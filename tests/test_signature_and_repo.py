import struct
import unittest

MAGIC = 0x4C434D53
VERSION = 1
ALGO = 1
HEADER_FMT = '<IBBHI32sH'
HEADER_SIZE = struct.calcsize(HEADER_FMT)


def is_valid_repo_format(repo: str) -> bool:
    if not repo or len(repo) > 95:
        return False
    if repo.count('/') != 1:
        return False
    owner, name = repo.split('/')
    if not owner or not name:
        return False
    allowed = set('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.')
    return all(c in allowed for c in owner + name)


def validate_sig_blob(blob: bytes, image: bytes):
    if len(blob) < HEADER_SIZE:
        return False, 'too_short'

    magic, version, algo, _reserved, fw_len, fw_hash, sig_len = struct.unpack(HEADER_FMT, blob[:HEADER_SIZE])
    if magic != MAGIC:
        return False, 'bad_magic'
    if version != VERSION:
        return False, 'bad_version'
    if algo != ALGO:
        return False, 'bad_algorithm'
    if fw_len != len(image):
        return False, 'bad_length'

    import hashlib
    calc = hashlib.sha256(image).digest()
    if fw_hash != calc:
        return False, 'bad_hash'

    if sig_len == 0 or len(blob) != HEADER_SIZE + sig_len:
        return False, 'bad_sig_length'

    return True, 'ok'


class SignatureFormatTests(unittest.TestCase):
    def make_blob(self, image: bytes, sig: bytes, algo=ALGO, fw_len=None, hash_override=None):
        import hashlib
        fw_hash = hash_override if hash_override is not None else hashlib.sha256(image).digest()
        if fw_len is None:
            fw_len = len(image)
        header = struct.pack(HEADER_FMT, MAGIC, VERSION, algo, 0, fw_len, fw_hash, len(sig))
        return header + sig

    def test_valid_signature_header(self):
        image = b'A' * 1024
        blob = self.make_blob(image, b'\x30\x01\x00')
        self.assertEqual(validate_sig_blob(blob, image), (True, 'ok'))

    def test_invalid_signature(self):
        image = b'A' * 128
        blob = self.make_blob(image, b'')
        self.assertEqual(validate_sig_blob(blob, image), (False, 'bad_sig_length'))

    def test_wrong_length(self):
        image = b'A' * 64
        blob = self.make_blob(image, b'\x30\x01\x00', fw_len=65)
        self.assertEqual(validate_sig_blob(blob, image), (False, 'bad_length'))

    def test_tampered_firmware(self):
        image = b'A' * 32
        blob = self.make_blob(image, b'\x30\x01\x00')
        tampered = b'B' * 32
        self.assertEqual(validate_sig_blob(blob, tampered), (False, 'bad_hash'))

    def test_wrong_algorithm(self):
        image = b'A' * 32
        blob = self.make_blob(image, b'\x30\x01\x00', algo=99)
        self.assertEqual(validate_sig_blob(blob, image), (False, 'bad_algorithm'))

    def test_corrupt_signature_file(self):
        image = b'A' * 32
        self.assertEqual(validate_sig_blob(b'corrupt', image), (False, 'too_short'))


class RepoValidationTests(unittest.TestCase):
    def test_valid_repo(self):
        self.assertTrue(is_valid_repo_format('AchimPieters/esp32-lifecycle-manager'))

    def test_invalid_repo_url(self):
        self.assertFalse(is_valid_repo_format('https://github.com/AchimPieters/esp32-lifecycle-manager'))

    def test_invalid_repo_multi_slash(self):
        self.assertFalse(is_valid_repo_format('owner/repo/extra'))


if __name__ == '__main__':
    unittest.main()
