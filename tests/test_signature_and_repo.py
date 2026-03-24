import struct
import unittest
import csv
import re
from pathlib import Path

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


def parse_version(value: str):
    if value is None:
        return None
    value = value.strip()
    if value.lower().startswith('v'):
        value = value[1:]
    parts = value.split('.')
    if len(parts) != 3:
        return None
    try:
        major, minor, patch = (int(parts[0]), int(parts[1]), int(parts[2]))
    except ValueError:
        return None
    return major, minor, patch


def compare_version(a: str, b: str):
    pa = parse_version(a)
    pb = parse_version(b)
    if pa is None or pb is None:
        raise ValueError('invalid version')
    if pa[0] != pb[0]:
        return pa[0] - pb[0]
    if pa[1] != pb[1]:
        return pa[1] - pb[1]
    return pa[2] - pb[2]


def should_trigger_powercycle_reset(current_count: int, reset_reason: str, min_threshold=10, max_threshold=12):
    allowed_reasons = {'POWERON', 'EXT', 'SW'}
    if reset_reason not in allowed_reasons:
        return False, 0
    count = current_count + 1
    if count > max_threshold:
        return False, 0
    return count >= min_threshold, count


def is_valid_ota_transition(current: str, nxt: str):
    allowed = {
        'IDLE': {'CHECKING_RELEASE'},
        'CHECKING_RELEASE': {'IDLE', 'DOWNLOADING', 'FAILED'},
        'DOWNLOADING': {'VERIFYING', 'FAILED'},
        'VERIFYING': {'STAGING', 'FAILED'},
        'STAGING': {'ACTIVATING', 'FAILED'},
        'ACTIVATING': {'REBOOTING', 'FAILED'},
        'FAILED': {'CHECKING_RELEASE', 'IDLE'},
        'REBOOTING': set(),
    }
    return nxt in allowed.get(current, set())


def required_ota_failure_reasons():
    return {
        'release_api_failure',
        'missing_asset',
        'invalid_signature',
        'invalid_image_length',
        'partition_unavailable',
        'boot_partition_set_failure',
        'http_failure',
    }


def parse_partition_value(value: str) -> int:
    v = value.strip().lower()
    if v.endswith('k'):
        return int(v[:-1], 0) * 1024
    if v.endswith('m'):
        return int(v[:-1], 0) * 1024 * 1024
    return int(v, 0)


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


class VersionLogicTests(unittest.TestCase):
    def test_parse_version_with_prefix(self):
        self.assertEqual(parse_version('v1.2.3'), (1, 2, 3))

    def test_parse_version_rejects_invalid(self):
        self.assertIsNone(parse_version('1.2'))

    def test_compare_version_higher_patch(self):
        self.assertGreater(compare_version('1.2.4', '1.2.3'), 0)

    def test_compare_version_invalid_raises(self):
        with self.assertRaises(ValueError):
            compare_version('x', '1.0.0')


class PowercycleLogicTests(unittest.TestCase):
    def test_below_window_increments_without_reset(self):
        should_reset, count = should_trigger_powercycle_reset(5, 'EXT')
        self.assertFalse(should_reset)
        self.assertEqual(count, 6)

    def test_threshold_reaches_reset(self):
        should_reset, count = should_trigger_powercycle_reset(9, 'POWERON')
        self.assertTrue(should_reset)
        self.assertEqual(count, 10)

    def test_maximum_window_still_triggers_reset(self):
        should_reset, count = should_trigger_powercycle_reset(11, 'SW')
        self.assertTrue(should_reset)
        self.assertEqual(count, 12)

    def test_non_allowed_reason_clears_path(self):
        should_reset, count = should_trigger_powercycle_reset(9, 'PANIC')
        self.assertFalse(should_reset)
        self.assertEqual(count, 0)

    def test_above_window_resets_counter_without_factory_reset(self):
        should_reset, count = should_trigger_powercycle_reset(12, 'SW')
        self.assertFalse(should_reset)
        self.assertEqual(count, 0)


class OtaStateMachineTests(unittest.TestCase):
    def test_happy_flow_is_valid(self):
        flow = ['IDLE', 'CHECKING_RELEASE', 'DOWNLOADING', 'VERIFYING', 'STAGING', 'ACTIVATING', 'REBOOTING']
        for i in range(len(flow) - 1):
            self.assertTrue(is_valid_ota_transition(flow[i], flow[i + 1]))

    def test_failure_transition_is_valid(self):
        self.assertTrue(is_valid_ota_transition('VERIFYING', 'FAILED'))

    def test_invalid_skip_is_rejected(self):
        self.assertFalse(is_valid_ota_transition('CHECKING_RELEASE', 'ACTIVATING'))

    def test_failed_state_recovery_paths(self):
        self.assertTrue(is_valid_ota_transition('FAILED', 'CHECKING_RELEASE'))
        self.assertTrue(is_valid_ota_transition('FAILED', 'IDLE'))

    def test_failed_state_cannot_jump_to_downloading(self):
        self.assertFalse(is_valid_ota_transition('FAILED', 'DOWNLOADING'))

    def test_rebooting_is_terminal(self):
        self.assertFalse(is_valid_ota_transition('REBOOTING', 'IDLE'))

    def test_failure_reason_taxonomy_is_complete(self):
        reasons = required_ota_failure_reasons()
        self.assertIn('invalid_signature', reasons)
        self.assertIn('partition_unavailable', reasons)
        self.assertEqual(len(reasons), 7)


class PartitionLayoutTests(unittest.TestCase):
    @staticmethod
    def _read_partitions():
        partitions = Path(__file__).resolve().parents[1] / 'partitions.csv'
        parsed = []
        with partitions.open('r', encoding='utf-8') as f:
            rows = csv.reader(line for line in f if line.strip() and not line.strip().startswith('#'))
            for row in rows:
                if len(row) >= 5:
                    parsed.append({
                        'name': row[0].strip(),
                        'type': row[1].strip(),
                        'subtype': row[2].strip(),
                        'offset': parse_partition_value(row[3]),
                        'size': parse_partition_value(row[4]),
                        'flags': row[5].strip() if len(row) > 5 else '',
                    })
        return parsed

    def test_layout_requires_4mb_or_more(self):
        max_end = 0
        for row in self._read_partitions():
            max_end = max(max_end, row['offset'] + row['size'])

        self.assertLessEqual(max_end, 0x400000, f'Partition end exceeds 4MB flash: 0x{max_end:x}')

    def test_layout_contains_dual_ota_and_otadata(self):
        names = {row['name'] for row in self._read_partitions()}

        self.assertIn('otadata', names)
        self.assertIn('nvs_keys', names)
        self.assertIn('ota_0', names)
        self.assertIn('ota_1', names)

    def test_nvs_keys_partition_is_encrypted(self):
        rows = self._read_partitions()
        nvs_keys = next((row for row in rows if row['name'] == 'nvs_keys'), None)
        self.assertIsNotNone(nvs_keys)
        self.assertEqual(nvs_keys['flags'], 'encrypted')

    def test_ota_slot_sizes_match(self):
        rows = self._read_partitions()
        ota0 = next((row for row in rows if row['name'] == 'ota_0'), None)
        ota1 = next((row for row in rows if row['name'] == 'ota_1'), None)
        self.assertIsNotNone(ota0)
        self.assertIsNotNone(ota1)
        self.assertEqual(ota0['size'], ota1['size'])

    def test_partitions_do_not_overlap(self):
        rows = sorted(self._read_partitions(), key=lambda r: r['offset'])
        for i in range(len(rows) - 1):
            current_end = rows[i]['offset'] + rows[i]['size']
            next_offset = rows[i + 1]['offset']
            self.assertLessEqual(
                current_end,
                next_offset,
                f"Partition overlap: {rows[i]['name']} (end=0x{current_end:x}) "
                f"overlaps {rows[i + 1]['name']} (offset=0x{next_offset:x})",
            )


class SourceHardeningTests(unittest.TestCase):
    @staticmethod
    def _github_update_source() -> str:
        path = Path(__file__).resolve().parents[1] / 'main' / 'github_update.c'
        return path.read_text(encoding='utf-8')

    def test_ota_transition_guard_exists_in_source(self):
        src = self._github_update_source()
        self.assertIn('static bool ota_transition_allowed', src)
        self.assertIn('Rejected invalid OTA transition', src)
        self.assertIn('ESP_ERR_INVALID_STATE', src)

    def test_cleanup_preserves_specific_failure_reason(self):
        src = self._github_update_source()
        self.assertIn('const char *failure_reason = OTA_REASON_HTTP_FAILURE;', src)
        self.assertIn('failure_reason = OTA_REASON_INVALID_SIGNATURE;', src)
        self.assertIn('failure_reason = OTA_REASON_INVALID_IMAGE_LENGTH;', src)
        self.assertIn('ota_persist_state(OTA_STATE_FAILED, ret, NULL, release_version, NULL, failure_reason);', src)


class SecurityProcessTests(unittest.TestCase):
    def test_key_management_doc_exists_with_required_sections(self):
        doc = (Path(__file__).resolve().parents[1] / 'docs' / 'security' / 'key-management.md')
        self.assertTrue(doc.exists(), 'docs/security/key-management.md is missing')
        text = doc.read_text(encoding='utf-8')
        self.assertIn('OTA signing key lifecycle', text)
        self.assertIn('NVS encryption provisioning flow', text)
        self.assertIn('Rotation policy', text)

    def test_nvs_provisioning_script_exists(self):
        script = (Path(__file__).resolve().parents[1] / 'scripts' / 'provision_nvs_keys.sh')
        self.assertTrue(script.exists(), 'scripts/provision_nvs_keys.sh is missing')
        text = script.read_text(encoding='utf-8')
        self.assertIn('generate-key', text)
        self.assertIn('--flash', text)
        self.assertIn('--device-id', text)
        self.assertIn('--audit-log', text)
        self.assertIn('sha256sum', text)


class ConsistencyTests(unittest.TestCase):
    def test_ci_targets_are_declared_in_component_targets(self):
        workflow = (Path(__file__).resolve().parents[1] / '.github' / 'workflows' / 'ci.yml').read_text(encoding='utf-8')
        component = (Path(__file__).resolve().parents[1] / 'idf_component.yml').read_text(encoding='utf-8')

        ci_match = re.search(r'target:\s*\[([^\]]+)\]', workflow)
        self.assertIsNotNone(ci_match, 'CI target matrix not found in workflow')
        ci_targets = {item.strip().strip('"\'') for item in ci_match.group(1).split(',') if item.strip()}

        component_targets = set(re.findall(r'^\s*-\s*"([^"]+)"\s*$', component, flags=re.MULTILINE))
        self.assertTrue(ci_targets, 'No CI targets parsed')
        self.assertTrue(component_targets, 'No component targets parsed')
        self.assertTrue(ci_targets.issubset(component_targets),
                        f'CI targets {sorted(ci_targets)} must be subset of component targets {sorted(component_targets)}')

    def test_support_matrix_mentions_ci_targets(self):
        workflow = (Path(__file__).resolve().parents[1] / '.github' / 'workflows' / 'ci.yml').read_text(encoding='utf-8')
        support_matrix = (Path(__file__).resolve().parents[1] / 'docs' / 'support-matrix.md').read_text(encoding='utf-8')

        ci_match = re.search(r'target:\s*\[([^\]]+)\]', workflow)
        self.assertIsNotNone(ci_match, 'CI target matrix not found in workflow')
        ci_targets = {item.strip().strip('"\'') for item in ci_match.group(1).split(',') if item.strip()}

        for target in ci_targets:
            self.assertIn(target, support_matrix, f'Target {target} missing from support matrix documentation')

    def test_esp32c5_target_and_c61_not_supported_doc(self):
        support_matrix = (Path(__file__).resolve().parents[1] / 'docs' / 'support-matrix.md').read_text(encoding='utf-8')
        component = (Path(__file__).resolve().parents[1] / 'idf_component.yml').read_text(encoding='utf-8')
        self.assertIn('ESP32-C61 (not currently targeted in this repository)', support_matrix)
        self.assertIn('"esp32c5"', component)


if __name__ == '__main__':
    unittest.main()
