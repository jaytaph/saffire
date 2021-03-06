<?php

namespace Saffire;

class UnitTester {
    protected $_it;                     // Directory iterator
    protected $_outputs;                // Different outputs;
    protected $_results = array();      // The actual results
    protected $_current = array();      // Information about the current test(s)

    protected $_timeStart;              // Time started
    protected $_timeEnd;                // Time ended

    protected $_mandatoryHeaders = array("title", "author");


    protected $_saffireBinary = null;       // Actual binary to test
    protected $_saffireBinaryVersion;       // Actual binary version (for version checking)

    // Result chars
    const PASS = "\033[32m-\033[0m";
    const FAIL = "\033[31mX\033[0m";
    const IGNORE = "\033[33;1mI\033[0m";
    const SKIP = "\033[33;1mS\033[0m";

    /**
     *
     */
    function __construct(\iterator $it) {
        $this->_it = $it;
        $this->_outputs = new \SplObjectStorage();


        // Get the binary to test from the environment
        $this->_saffireBinary = getenv("SAFFIRE_TEST_BIN");
        if (! $this->_saffireBinary) {
            print "Please set the SAFFIRE_TEST_BIN environment setting to the correct saffire binary version.\n\n";
            exit(1);
        }

        // Execute (and thus: find) the binary to find version
        exec($this->_saffireBinary." version --long", $output, $result);
        if ($result != 0) {
            print "Cannot find the test binary set by the SAFFIRE_TEST_BIN environment, or error while fetching its version.\n\n";
            exit(1);
        }

        // Parse version and store it so we can use this info for some of the header tags (since, until etc)
        if (!preg_match("/([0-9\.]+)/", $output[0], $match)) {
            print "Cannot find the test binary set by the SAFFIRE_TEST_BIN environment, or error while fetching its version.\n\n";
            exit(1);
        }
        $this->_saffireBinaryVersion = $match[1];
    }


    /**
     * Adds an output class
     *
     * @param Output\iOutput $output
     */
    function addOutput(Output\iOutput $output) {
        $this->_outputs->attach($output);
    }


    /**
     * Removes an output class if was added before
     *
     * @param Output\iOutput $output
     */
    function removeOutput(Output\iOutput $output) {
        if ($this->_outputs->contains($output)) {
            $this->_outputs->detach($output);
        }
    }


    /**
     * Run the tester
     */
    function run() {
        $this->_init();

        // Header
        $this->_output("Saffire Test Suite v0.1 - The Saffire Group\n");

        // Timer
        $this->_startTimer();
        $this->_output("Start time: ".date("H:i:s", time())."\n");

        // Iterate and test all files
        foreach ($this->_it as $filename) {
            if (is_array($filename)) $filename = $filename[0];
            $this->_runTest($filename);
        }

        // Output end time
        $this->_output("End time: ".date("H:i:s", time()));
        $this->_output(" (".$this->_timerLap()." seconds running time)\n");

        // Output status
        $this->_output("Status\n");
        $this->_output("    Test files  : ".sprintf("%5d", $this->_results['total_files'])."\n");
        $this->_output("    Total tests : ".sprintf("%5d", $this->_results['total_tests'])."\n");
        if ($this->_results['failed'] == 0) $this->_output("\033[42;37;1m");
        $this->_output("    Passed      : ".sprintf("%5d", $this->_results['passed'])." (".$this->_perc($this->_results['passed'], $this->_results['total_tests']).")");
        $this->_output("\033[0m\n");
        if ($this->_results['failed'] > 0) $this->_output("\033[41;33;1m");
        $this->_output("    Failed      : ".sprintf("%5d", $this->_results['failed'])." (".$this->_perc($this->_results['failed'], $this->_results['total_tests']).")");
        $this->_output("\033[0m\n");
        $this->_output("    Ignored     : ".sprintf("%5d", $this->_results['ignored'])." (".$this->_perc($this->_results['ignored'], $this->_results['total_tests']).")\n");
        $this->_output("    Skipped     : ".sprintf("%5d", $this->_results['skipped'])." (".$this->_perc($this->_results['skipped'], $this->_results['total_tests']).")\n");
        $this->_output("\n");


        foreach ($this->_results['errors'] as $error) {
            $this->_output("=============================\n");
            $this->_output($error);
            $this->_output("\n");
        }

        return $this->_results['failed'] > 0 ? 1 : 0;
    }

    protected function _perc($p, $t) {
        if ($t == 0) return "0.00%";
        return round($p / $t * 100, 2)."%";
    }


    /**
     * Initialize information
     */
    protected function _init() {
        $this->_results = array();
        $this->_results['total_files'] = 0;
        $this->_results['total_tests'] = 0;
        $this->_results['passed'] = 0;
        $this->_results['failed'] = 0;
        $this->_results['skipped'] = 0;
        $this->_results['ignored'] = 0;

        $this->_results['errors'] = array();
        $this->_current = array();
    }


    /**
     * Start the timer
     */
    protected function _startTimer() {
        $this->_timeStart = microtime(true);
    }


    /**
     * Stop the timer
     */
    protected function _timerLap() {
        $tmp = microtime(true);
        $tmp = round($tmp - $this->_timeStart, 2);
        return $tmp;
    }


    /**
     * Outputs a string to all connected output classes
     *
     * @param $str
     */
    protected function _output($str) {
        // Output to all the output functions
        foreach ($this->_outputs as $output) {
            /** @var $output Output\iOutput */
            $output->output($str);
        }
    }


    /**
     * Run one specific test
     */
    protected function _runTest($filename) {
        // Increase the number of test FILES we are testing
        $this->_results['total_files']++;

        // Initialize the current suite result
        $this->_current = array();
        $this->_current['filename'] = $filename;
        $this->_current['lineno'] = 1;
        $this->_current['tags'] = array();

        $this->_output($this->_current['filename']." : ");

        // Read header
        $f = file_get_contents($filename);
        if ($f == null) {
            $this->_results['failed']++;
        }
        $this->_current['contents'] = $f;

        // Find header and body
        $pattern = "/^\*\*\*\*+\n/m";
        $header_and_body = preg_split($pattern, $this->_current['contents'], 4, PREG_OFFSET_CAPTURE);
        if (count($header_and_body) != 2) {
            $this->_output("Error finding header in file\n");
            return;
        }

        if (! $this->_parseHeader($header_and_body[0])) {
            $this->_output("Error parsing header in file\n");
            return;
        }

        $this->_output($this->_current['tags']['title']." : [");

        // Do each test, separated by @@@
        $this->_runFunctionalTests($header_and_body[1]);

        $this->_output("]\n");
    }


    protected function _runFunctionalTests($body) {
        // Split each test
        $pattern = "/^\@\@\@\@+\n/m";
        $tests = preg_split($pattern, $body);

        $this->_current['testno'] = 1;

        foreach ($tests as $test) {
            // Run the actual test
            $result = $this->_runFunctionalTest($test, $this->_current['testno'], $this->_current['lineno']);
            $this->_results['total_tests']++;

            $this->_current['lineno'] += countnewlines($test)+1;
            $this->_current['testno']++;

            switch ($result) {
                case self::PASS :
                    $this->_results['passed']++;
                    break;
                case self::FAIL :
                    $this->_results['failed']++;
                    break;
                case self::IGNORE :
                    $this->_results['ignored']++;
                    break;
                case self::SKIP :
                    $this->_results['skipped']++;
                    break;
            }

            if (($this->_current['testno']-2) % 10 == 0 && ($this->_current['testno']-2) != 0) {
                $this->_output("][");
            }
            $this->_output($result);
        }
    }


    /**
     */
    protected function _runFunctionalTest($test, $testno, $lineno) {
        $ignore = false;

        // Check if we need to skip this test.
        if (preg_match("/^!skip/i", $test)) return self::SKIP;

        // Check if we need to ignore results of this test.
        if (preg_match("/^!ignore/i", $test)) $ignore = true;


        $tmpDir = sys_get_temp_dir();

        // Don't expect any output
        $outputExpected = false;

        // Split source and expected output
        $pattern = "/^(=+|~+)\n/m";
        $tmp = preg_split($pattern, $test, 2, PREG_SPLIT_DELIM_CAPTURE);

        // When it's split by =====, we want strict matching, ~~~~ fuzzy: as long as the text is somewhere in the output
        $match_strict = ($tmp[1][1] == '=');

        // Generate temp file(name)
        $tmpFile = tempnam($tmpDir, "saffire_test");

        // Create tempfile with source and optional expectation
        file_put_contents($tmpFile.".sf", $tmp[0]);
        $tmp[1] = trim($tmp[1]);
        if (! empty($tmp[1])) {
            $outputExpected = true;
            $tmp[1] = preg_replace("/\n$/", "", $tmp[2]);
            file_put_contents($tmpFile.".exp", $tmp[2]);
        }


        // Run saffire test
        $ret = self::FAIL;  // Assume the worst
        exec($this->_saffireBinary." ".$tmpFile.".sf > ".$tmpFile.".out 2>&1", $output, $result);

        // No output expected and result is not 0. So FAIL
        if (! $outputExpected && $result != 0) {
            $ret = self::FAIL;
            $tmp = "";
            $tmp .= "Error in ".$this->_current['filename']." (test $testno, line $lineno)\n";
            $tmp .= "Expected exitcode 0, got exitcode $result";
            $tmp .= "\n";
            $this->_results['errors'][] = $tmp;

            goto cleanup;   // Yes, goto. Live with the pain
        }

        // No output expected, but result is 0. So PASS
        if (! $outputExpected && $result == 0) {
            $ret = self::PASS;
            goto cleanup;   // Yes, goto. Live with the pain
        }


        if ($outputExpected) {
            // We expect certain output. Let's try and diff it..

            if ($match_strict) {
                $error = ! diff_it($tmpFile . ".out", $tmpFile . ".exp", $output);
            } else {
                $error = ! find_lines($tmpFile . ".out", $tmpFile . ".exp", $output);
            }

            if ($error) {
                $tmp = "";
                $tmp .= "Error in ".$this->_current['filename']." (test $testno, line $lineno)\n";
                $tmp .= join("\n", $output);
                $tmp .= "\n";

                // @TODO: when we are at X errors, quit anyway
                $this->_results['errors'][] = $tmp;

                $ret = self::FAIL;
                goto cleanup;
            } else {
                $ret = self::PASS;
                goto cleanup;
            }
        }

cleanup:
        // Unlink all temp files
        @unlink($tmpFile);
        @unlink($tmpFile.".sf");
        @unlink($tmpFile.".sfc");
        @unlink($tmpFile.".exp");
        @unlink($tmpFile.".out");
        @unlink($tmpFile.".diff");


        return $ignore ? self::IGNORE : $ret;
    }


    /**
     * Parses and validates the tags inside the headers
     */
    protected function _parseHeader($header) {
        foreach (explode("\n", $header) as $line) {
            $this->_current['lineno']++;
            $line = trim($line);
            if (empty($line)) continue;     // Skip empty lines
            if ($line[0] == "#") continue;  // Skip comments

            $tmp = explode(":", $line, 2);
            if (count($tmp) != 2) {
                $this->_output("Cannot find tag on line ".$this->_current['lineno']."\n");
                return false;
            }

            list($key, $value) = $tmp;
            $this->_current['tags'][strtolower($key)] = trim($value);
        }

        // Find all the mandatory headers
        $tmp = array_diff($this->_mandatoryHeaders, array_keys($this->_current['tags']));
        if (count($tmp) > 0) {
            $this->_output("Error finding mandatory headers: ".join(", ", $tmp)."\n");
            return false;
        }
        return true;
    }

}


function find_lines($in_name, $exp_name, &$output) {
    $output = array();

    $f1 = @file_get_contents($in_name);
    $f2 = @file_get_contents($exp_name);

    $f1 = trim($f1);
    $f2 = trim($f2);

    return (strstr($f1, $f2) !== false);
}


/**
 */
function diff_it($in_name, $exp_name, &$output) {
    $output = array();

    $f1 = @file($in_name);
    $f2 = @file($exp_name);
    if (is_bool($f1)) $f1 = array();
    if (is_bool($f2)) $f2 = array();

    // Sync sizes of f1 and f2
    while (count($f1) < count($f2)) $f1[] = "";
    while (count($f2) < count($f1)) $f2[] = "";

    reset($f1);
    reset($f2);

    for ($i=0; $i!=count($f2); $i++) {
        $v1 = current($f1);
        $v2 = current($f2);
        next($f1);
        next($f2);

        $v1 = sanitize_line($v1);
        $v2 = sanitize_line($v2);

        // Complete match
        if ($v1 == $v2) continue;

        if (empty($v2) || stristr($v1, $v2) === false) {
            $output[] = "+++ '".$v1."'";
            $output[] = "--- '".$v2."'";
        }


    }

    return count($output) == 0;
}


/*
 * Sanitizes a line
 */
function sanitize_line($line) {
    $line = preg_replace("/^Error: line \d: /", "", $line);
    $line = preg_replace("/^Error in line \d: /", "", $line);

    $line = preg_replace("/\n$/", "", $line);
    return $line;
}

function countnewlines($s) {
    $nl = 0;
    for ($i=0; $i!=strlen($s); $i++) {
        if ($s[$i] == "\n") $nl++;
    }
    return $nl;
}
