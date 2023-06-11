"""
Python based fuse unit tests.
"""

import os
import subprocess
import time
import unittest

TEST_CONTAINER_PATH = "/tmp/oncefs-unit.ofs"
TEST_CONTAINER_SIZE_BYTES = 100_000


class TestBase(unittest.TestCase):
    def setUp(self):
        """
        Set up.
        """
        count = TEST_CONTAINER_SIZE_BYTES // 1000

        subprocess.check_call(
            [
                "dd",
                "if=/dev/urandom",
                f"of={TEST_CONTAINER_PATH}",
                "bs=1k",
                f"count={count}",
            ],
            stderr=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
        )
        subprocess.check_call(
            ["./fuse", "--format", TEST_CONTAINER_PATH, "mountpoint"]
        )

        self._container_size = TEST_CONTAINER_SIZE_BYTES

    def tearDown(self):
        """
        Tear down.
        """
        subprocess.call(
            ["fusermount", "-u", "mountpoint"],
            stderr=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
        )

        # subprocess.call(["cp", TEST_CONTAINER_PATH, "test.ofs"])

        subprocess.check_call(["rm", TEST_CONTAINER_PATH])

    def _remount(self):
        """
        Test unmounting and then loading.
        """
        subprocess.check_call(["fusermount", "-u", "mountpoint"])
        subprocess.check_call(["./fuse", TEST_CONTAINER_PATH, "mountpoint"])


class TestFuse(TestBase):
    def test_create(self):
        """
        Test creating nodes.
        """
        subprocess.check_call(["mkdir", "-p", "mountpoint/alpha"])
        subprocess.check_call(["mkdir", "-p", "mountpoint/bravo"])
        subprocess.check_call(["mkdir", "-p", "mountpoint/alpha/apple"])
        subprocess.check_call(["mkdir", "-p", "mountpoint/bravo/banana"])
        subprocess.check_call(["mkdir", "-p", "mountpoint/alpha/apple/a"])
        subprocess.check_call(["mkdir", "-p", "mountpoint/bravo/banana/b"])

        with open("mountpoint/alpha/foo", "w") as output_stream:
            output_stream.write("Hello world!")

        self._remount()

        actual = subprocess.check_output(["find", "mountpoint/"]).decode().split()
        expected = [
            "mountpoint/",
            "mountpoint/alpha",
            "mountpoint/alpha/apple",
            "mountpoint/alpha/apple/a",
            "mountpoint/alpha/foo",
            "mountpoint/bravo",
            "mountpoint/bravo/banana",
            "mountpoint/bravo/banana/b",
        ]

        self.assertEqual(actual, expected)

    def test_create_remount(self):
        """
        Test creating after remounting.
        """
        subprocess.check_call(["mkdir", "-p", "mountpoint/alpha"])
        with open("mountpoint/alpha/apple", "w") as output_stream:
            output_stream.write("Hello world!")

        self._remount()

        subprocess.check_call(["mkdir", "-p", "mountpoint/bravo"])
        with open("mountpoint/bravo/banana", "w") as output_stream:
            output_stream.write("Hello there")

        actual = subprocess.check_output(["find", "mountpoint/"]).decode().split()
        expected = [
            "mountpoint/",
            "mountpoint/alpha",
            "mountpoint/alpha/apple",
            "mountpoint/bravo",
            "mountpoint/bravo/banana",
        ]

        self.assertEqual(actual, expected)

    def test_write_read(self):
        """
        Test writing, remounting, then reading.
        """
        expected = "Hello world!"
        with open("mountpoint/foo", "w") as output_stream:
            output_stream.write(expected)

        # Unrelated data
        with open("mountpoint/bar", "w") as output_stream:
            output_stream.write("bork bork bork")

        with open("mountpoint/foo") as input_stream:
            actual = input_stream.read()

        self.assertEqual(actual, expected)

        self._remount()

        with open("mountpoint/foo") as input_stream:
            actual = input_stream.read()

        self.assertEqual(actual, expected)

    def test_write_append(self):
        """
        Test writing in append mode
        """
        with open("mountpoint/foo", "w") as output_stream:
            output_stream.write("Hello")

        with open("mountpoint/foo", "a") as output_stream:
            output_stream.seek(6)
            output_stream.write(" world!")

        expected = "Hello world!"

        with open("mountpoint/foo") as input_stream:
            actual = input_stream.read()
        self.assertEqual(actual, expected)

        self._remount()

        with open("mountpoint/foo") as input_stream:
            actual = input_stream.read()
        self.assertEqual(actual, expected)

    # Not yet supported
    # def test_write_plus(self):
    #     """
    #     Test writing in read write
    #     """
    #     with open("mountpoint/foo", "w") as output_stream:
    #         output_stream.write("Hello world!")

    #     with open("mountpoint/foo", "w+") as output_stream:
    #         output_stream.seek(6)
    #         output_stream.write("there")

    #     expected = "Hello there"

    #     with open("mountpoint/foo") as input_stream:
    #         actual = input_stream.read()
    #     self.assertEqual(actual, expected)

    #     self._remount()

    #     with open("mountpoint/foo") as input_stream:
    #         actual = input_stream.read()
    #     self.assertEqual(actual, expected)

    def test_write_read_big(self):
        """
        Test multi block writing, remounting, then reading.
        """
        capacity = TEST_CONTAINER_SIZE_BYTES

        expected = os.urandom(int(capacity * 0.3))
        with open("mountpoint/foo", "wb") as output_stream:
            output_stream.write(expected)

        with open("mountpoint/foo", "rb") as input_stream:
            actual = input_stream.read()

        self.assertEqual(actual, expected)

        self._remount()

        # Unrelated data
        with open("mountpoint/bar", "wb") as output_stream:
            output_stream.write(os.urandom(int(capacity * 0.3)))

        # Read original data
        with open("mountpoint/foo", "rb") as input_stream:
            actual = input_stream.read()

        self.assertEqual(actual, expected)

    def test_symlink(self):
        """
        Test creating a symlink, remounting, then reading.
        """
        expected = "Hello world!"
        with open("mountpoint/foo", "w") as output_stream:
            output_stream.write(expected)

        subprocess.check_call(["ln", "-s", "foo", "mountpoint/bar"])

        self._remount()

        with open("mountpoint/bar") as input_stream:
            actual = input_stream.read()

        self.assertEqual(actual, expected)

    def test_rename(self):
        """
        Test renaming nodes.
        """
        subprocess.check_call(["mkdir", "-p", "mountpoint/alpha"])
        subprocess.check_call(["mkdir", "-p", "mountpoint/bravo"])
        subprocess.check_call(["mkdir", "-p", "mountpoint/alpha/apple"])
        subprocess.check_call(["mkdir", "-p", "mountpoint/bravo/banana"])

        subprocess.check_call(["mv", "mountpoint/alpha/apple", "mountpoint/bravo"])
        subprocess.check_call(["mv", "mountpoint/bravo/banana", "mountpoint/alpha/bee"])

        self._remount()

        actual = subprocess.check_output(["find", "mountpoint/"]).decode().split()
        expected = [
            "mountpoint/",
            "mountpoint/alpha",
            "mountpoint/alpha/bee",
            "mountpoint/bravo",
            "mountpoint/bravo/apple",
        ]

        self.assertEqual(actual, expected)

    def test_rename(self):
        """
        Test renaming nodes.
        """
        with open("mountpoint/foo", "w") as output_stream:
            output_stream.write("Hello world!")
        subprocess.check_call(["mkdir", "-p", "mountpoint/baz"])

        subprocess.check_call(["mv", "mountpoint/foo", "mountpoint/bar"])

        actual = subprocess.check_output(["find", "mountpoint/"]).decode().split()
        expected = [
            "mountpoint/",
            "mountpoint/bar",
            "mountpoint/baz",
        ]
        self.assertEqual(actual, expected)

        self._remount()

        actual = subprocess.check_output(["find", "mountpoint/"]).decode().split()
        self.assertEqual(actual, expected)

    def test_rename(self):
        """
        Test moving nodes to a different folder.
        """
        subprocess.check_call(["mkdir", "-p", "mountpoint/alpha"])
        with open("mountpoint/alpha/apple", "w") as output_stream:
            output_stream.write("Hello world!")
        subprocess.check_call(["mkdir", "-p", "mountpoint/bravo/banana"])

        subprocess.check_call(
            ["mv", "mountpoint/alpha/apple", "mountpoint/bravo/banana"]
        )

        expected = [
            "mountpoint/",
            "mountpoint/alpha",
            "mountpoint/bravo",
            "mountpoint/bravo/banana",
            "mountpoint/bravo/banana/apple",
        ]

        actual = subprocess.check_output(["find", "mountpoint/"]).decode().split()
        self.assertEqual(actual, expected)

        self._remount()

        actual = subprocess.check_output(["find", "mountpoint/"]).decode().split()
        self.assertEqual(actual, expected)

    def test_replace(self):
        """
        Test overwriting a node with another.
        """
        data = "Hello world!"
        with open("mountpoint/foo", "w") as output_stream:
            output_stream.write(data)

        with open("mountpoint/bar", "w") as output_stream:
            output_stream.write("bork bork")

        subprocess.check_call(["mv", "mountpoint/foo", "mountpoint/bar"])

        expected = [
            "mountpoint/",
            "mountpoint/bar",
        ]

        actual = subprocess.check_output(["find", "mountpoint/"]).decode().split()
        self.assertEqual(actual, expected)

        with open("mountpoint/bar") as input_stream:
            actual = input_stream.read()
        self.assertEqual(actual, data)

        self._remount()

        actual = subprocess.check_output(["find", "mountpoint/"]).decode().split()
        self.assertEqual(actual, expected)

        with open("mountpoint/bar") as input_stream:
            actual = input_stream.read()
        self.assertEqual(actual, data)

    def test_delete_node(self):
        """
        Test deleting nodes.
        """
        subprocess.check_call(["mkdir", "-p", "mountpoint/alpha"])
        subprocess.check_call(["mkdir", "-p", "mountpoint/bravo"])
        subprocess.check_call(["mkdir", "-p", "mountpoint/charlie"])
        subprocess.check_call(["touch", "mountpoint/alpha/apple"])
        subprocess.check_call(["touch", "mountpoint/bravo/banana"])
        subprocess.check_call(["touch", "mountpoint/charlie/cinnamon"])

        subprocess.check_call(["rm", "mountpoint/alpha/apple"])
        subprocess.check_call(["rmdir", "mountpoint/alpha"])
        subprocess.check_call(["rm", "-rf", "mountpoint/bravo"])
        with self.assertRaises(subprocess.CalledProcessError):
            subprocess.check_call(
                ["rm", "mountpoint/charlie"], stderr=subprocess.DEVNULL
            )

        self._remount()

        actual = subprocess.check_output(["find", "mountpoint/"]).decode().split()
        expected = [
            "mountpoint/",
            "mountpoint/charlie",
            "mountpoint/charlie/cinnamon",
        ]

        self.assertEqual(actual, expected)

    def test_delete_full(self):
        """
        Test deleting when filesystem is completely full.
        """
        subprocess.check_output(["touch", "mountpoint/foo"])

        expected = os.urandom(int(TEST_CONTAINER_SIZE_BYTES * 1.1))
        try:
            with open("mountpoint/bar", "wb") as output_stream:
                output_stream.write(expected)
        except OSError as exp:
            self.assertIn("No space left on device", str(exp))

        with self.assertRaises(subprocess.CalledProcessError):
            subprocess.check_output(
                ["rm", "mountpoint/bork"], stderr=subprocess.DEVNULL
            )

        subprocess.check_output(["rm", "mountpoint/bar"])

        actual = subprocess.check_output(["find", "mountpoint/"]).decode().split()
        expected = [
            "mountpoint/",
            "mountpoint/foo",
        ]

        self.assertEqual(actual, expected)

        self._remount()

        actual = subprocess.check_output(["find", "mountpoint/"]).decode().split()
        expected = [
            "mountpoint/",
            "mountpoint/foo",
        ]

        self.assertEqual(actual, expected)

    def test_delete_data(self):
        """
        Test deleting data.
        """
        with open("mountpoint/foo", "w") as output_stream:
            output_stream.write("Hello world!")

        # Unrelated
        with open("mountpoint/bar", "w") as output_stream:
            output_stream.write("bork bork bork")

        expected = "Hello!"  # Smaller
        with open("mountpoint/foo", "w") as output_stream:
            output_stream.write(expected)

        with open("mountpoint/foo") as input_stream:
            actual = input_stream.read()

        self.assertEqual(actual, expected)

        self._remount()

        with open("mountpoint/foo") as input_stream:
            actual = input_stream.read()

        self.assertEqual(actual, expected)

    def test_delete_data_big(self):
        """
        Test deleting data.
        """
        with open("mountpoint/foo", "wb") as output_stream:
            output_stream.write(os.urandom(1024 * 10))

        # Unrelated data
        with open("mountpoint/bar", "wb") as output_stream:
            output_stream.write(os.urandom(1024 * 10))

        expected = os.urandom(1024 * 5)  # smaller
        with open("mountpoint/foo", "wb") as output_stream:
            output_stream.write(expected)

        self._remount()

        with open("mountpoint/foo", "rb") as input_stream:
            actual = input_stream.read()

        self.assertEqual(actual, expected)

    def test_reuse(self):
        """
        Test reusing containers.
        """
        capacity = TEST_CONTAINER_SIZE_BYTES
        self.assertEqual(TEST_CONTAINER_SIZE_BYTES, capacity)

        def fill():
            """
            Helper to write a chunk of data then read it back.
            """
            expected = os.urandom(int(capacity * 0.8))  # Partially fill
            with open("mountpoint/foo", "wb") as output_stream:
                output_stream.write(expected)

            with open("mountpoint/foo", "rb") as input_stream:
                actual = input_stream.read()

            self.assertEqual(actual, expected)

        fill()
        fill()

        self._remount()
        fill()

        subprocess.check_call(["rm", "mountpoint/foo"])
        fill()

        subprocess.check_call(["rm", "mountpoint/foo"])
        self._remount()
        fill()

    def test_reuse_delete(self):
        """
        Test that "delete" containers can be reused
        """
        # Fill up a bunch of space
        with open("mountpoint/dummy", "wb") as output_stream:
            output_stream.write(b"0" * int(self._container_size * 0.8))

        # Fill the rest with deletes (slow)
        for _ in range(20):  # This number has no theoretical upper limit
            subprocess.check_call(["mkdir", "mountpoint/foo"])
            subprocess.check_call(["rmdir", "mountpoint/foo"])

    def test_file_symlink(self):
        """
        Test ops through a file symlink
        """
        with open("mountpoint/foo", "w") as output_stream:
            output_stream.write("Hello world!")

        # Unrelated data
        with open("mountpoint/bar", "w") as output_stream:
            output_stream.write("bork bork bork")
        subprocess.check_call(["mkdir", "-p", "mountpoint/baz"])

        # Linking
        subprocess.check_call(["ln", "-s", "../foo", "mountpoint/baz/fooish"])

        # Write through symlink
        expected = "testing"
        with open("mountpoint/baz/fooish", "w") as output_stream:
            output_stream.write(expected)

        with open("mountpoint/baz/fooish") as input_stream:
            actual = input_stream.read()

        self.assertEqual(actual, expected)

        self._remount()

        with open("mountpoint/baz/fooish") as input_stream:
            actual = input_stream.read()

        self.assertEqual(actual, expected)

    def test_dir_symlink(self):
        """
        Test ops through a directory symlink
        """
        subprocess.check_call(["mkdir", "-p", "mountpoint/alpha"])

        expected = "Hello world!"
        with open("mountpoint/alpha/apple", "w") as output_stream:
            output_stream.write(expected)

        # Unrelated data
        with open("mountpoint/bar", "w") as output_stream:
            output_stream.write("bork bork bork")

        subprocess.check_call(["mkdir", "-p", "mountpoint/bravo"])

        subprocess.check_call(["ln", "-s", "../alpha/apple", "mountpoint/bravo/banana"])

        with open("mountpoint/bravo/banana") as input_stream:
            actual = input_stream.read()

        self.assertEqual(actual, expected)

        self._remount()

        with open("mountpoint/bravo/banana") as input_stream:
            actual = input_stream.read()

        self.assertEqual(actual, expected)

    def test_rm_symlink(self):
        """
        Test removing a symlink
        """
        expected = "Hello world!"
        with open("mountpoint/foo", "w") as output_stream:
            output_stream.write(expected)
        subprocess.check_call(["mkdir", "-p", "mountpoint/bar"])

        subprocess.check_call(["ln", "-s", "foo", "mountpoint/fooish"])
        subprocess.check_call(["ln", "-s", "bar", "mountpoint/barish"])

        subprocess.check_call(["rm", "mountpoint/fooish"])
        subprocess.check_call(["rm", "mountpoint/barish"])

        expected = [
            "mountpoint/",
            "mountpoint/bar",
            "mountpoint/foo",
        ]
        actual = subprocess.check_output(["find", "mountpoint/"]).decode().split()
        self.assertEqual(actual, expected)

        self._remount()

        actual = subprocess.check_output(["find", "mountpoint/"]).decode().split()
        self.assertEqual(actual, expected)

    def test_set_time(self):
        """
        Test setting and getting time.
        """
        path = "mountpoint/foo"

        with open(path, "w") as output_stream:
            output_stream.write("Hello world!")

        expected = int(time.time())

        self.assertEqual(os.path.getatime(path), expected)
        self.assertEqual(os.path.getmtime(path), expected)
        self.assertEqual(os.path.getctime(path), expected)

        subprocess.check_call(["touch", "-t", "0001010000", path])

        expected = 946702800
        self.assertEqual(os.path.getatime(path), expected)
        self.assertEqual(os.path.getmtime(path), expected)
        self.assertEqual(os.path.getctime(path), expected)

        self._remount()

        self.assertEqual(os.path.getatime(path), expected)
        self.assertEqual(os.path.getmtime(path), expected)
        self.assertEqual(os.path.getctime(path), expected)


if __name__ == "__main__":
    unittest.main()
