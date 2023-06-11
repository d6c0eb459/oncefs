"""
Run unit tests under container re-use conditions (ie filesystem where all containers have
already been used at least once, but in which there is some space available because some
files were deleted).
"""
import os
import subprocess
import unittest


import test_unit


class TestReuse(test_unit.TestFuse):
    def setUp(self):
        super().setUp()

        # Trigger container re-use
        with open("mountpoint/dummy", "wb") as output_stream:
            output_stream.write(b"0" * int(self._container_size * 0.9))

        subprocess.check_call(["rm", "mountpoint/dummy"])

        with open("mountpoint/dummy", "wb") as output_stream:
            output_stream.write(b"0" * int(self._container_size * 0.1))

        subprocess.check_call(["rm", "mountpoint/dummy"])


if __name__ == "__main__":
    unittest.main()
