# Copyright (C) 2019 MindMaze
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import os
import sys
import unittest
import copy


class TapTestResult(unittest.TestResult):
    def __init__(self, stream):
        super(TapTestResult, self).__init__(self)
        self.stream = stream

    def _print_tap_result(self, result, test, directive=None):
        dirstr = ""
        if directive:
            dirstr = " # " + directive
        description = "{} : {}".format(test.id(), test.shortDescription())
        self.stream.write("{} {} {}{}\n".format(result,
                                                self.testsRun,
                                                description,
                                                dirstr))
        self.stream.flush()

    def addSuccess(self, test):
        super(TapTestResult, self).addSuccess(test)
        self._print_tap_result("ok", test)

    def addError(self, test, err):
        super(TapTestResult, self).addError(test, err)
        sys.stderr.write(self.errors[-1][1] + "\n")
        self._print_tap_result("not ok", test)

    def addFailure(self, test, err):
        super(TapTestResult, self).addFailure(test, err)
        sys.stderr.write(self.failures[-1][1] + "\n")
        self._print_tap_result("not ok", test)

    def addSkip(self, test, reason):
        super(TapTestResult, self).addSkip(test, reason)
        self._print_tap_result("ok", test, "SKIP " + reason)

    def addExpectedFailure(self, test, err):
        super(TapTestResult, self).addExpectedFailure(test, err)
        self._print_tap_result("ok", test)

    def addUnexpectedSuccess(self, test):
        super(TapTestResult, self).addUnexpectedSuccess(test)
        self._print_tap_result("not ok", test)


class TapTestRunner(unittest.TextTestRunner):
    def __init__(self, output = sys.stdout):
        super().__init__(self)
        self.stream = output

    def run(self, test):
        # Write TAP header. The testplan does not have to be at the beginning,
        # but it is better since this way the consumer knows how many have not
        # been run. To see specs of TAP:
        # https://testanything.org/tap-specification.html
        num_tests = test.countTestCases()
        self.stream.write("TAP version 13\n1..{}\n".format(num_tests))
        self.stream.flush()

        result = TapTestResult(self.stream)
        unittest.registerResult(result)
        try:
            rv = test(result)
            return len(rv.errors)
        except:
            return -1
        finally:
            unittest.removeResult(result)


if __name__ == '__main__':
    tests_dir = os.path.dirname(os.path.abspath(__file__))
    case_filter = os.environ.get('PY_RUN_CASE', '')
    pattern = 'test_*{0}*.py'.format(case_filter)

    loader = unittest.TestLoader()

    # python unittest discover modifies sys.path
    # (See: https://bugs.python.org/issue24247)
    # to prevent path mixup between the package being tested and an eventual
    # installed package, prevent any such change
    path_backup = copy.deepcopy(sys.path)
    tests = loader.discover(tests_dir, pattern=pattern)
    sys.path = path_backup

    runner = TapTestRunner()
    exit(runner.run(tests))
