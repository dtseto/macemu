#!/usr/bin/env python3
"""Unit tests for the external 68000 corpus adapter."""

import copy
import unittest

import m68000_json_adapter as adapter


def state(prefetch=(0xF000, 0xBEEF)):
    value = {f"d{i}": 0 for i in range(8)}
    value.update({f"a{i}": 0 for i in range(7)})
    value.update({
        "sr": 0x2700,
        "pc": 0x1000,
        "usp": 0x2000,
        "ssp": 0x3000,
        "prefetch": list(prefetch),
        "ram": [[0x1000, 0xF0], [0x1001, 0x00]],
    })
    return value


def indexed_test(extension_word=0xB600):
    return {
        "name": "000 MOVE.L d8(A0,Xn),D0 1010",
        "initial": state((0x1010, extension_word)),
        "final": state((0x1010, extension_word)),
        "transactions": [[0, 0, 0, 0x1000]],
        "length": 4,
    }


class AdapterTests(unittest.TestCase):
    def test_indexed_adaptation_preserves_source_metadata(self):
        source = indexed_test(0xB600)
        vector = adapter.normalize_test(source, "synthetic", 0,
                                         adapt_indexed_for_68020=True)

        self.assertEqual(vector["instruction_words"], [0x1010, 0xB000])
        self.assertTrue(vector["indexed_adapted"])
        self.assertEqual(vector["indexed"]["extension_word"], 0xB600)
        self.assertEqual(vector["indexed"]["adapted_extension_word"], 0xB000)
        self.assertEqual(vector["indexed"]["index_register"], 3)
        self.assertTrue(vector["indexed"]["index_is_address"])
        self.assertTrue(vector["indexed"]["index_is_word"])
        self.assertEqual(vector["indexed"]["scale"], 8)
        self.assertEqual(vector["indexed"]["scale_68000"], 1)
        self.assertEqual(vector["initial"]["prefetch"][1], 0xB600)

    def test_native_indexed_vector_is_not_rewritten(self):
        source = indexed_test(0xB600)
        vector = adapter.normalize_test(source, "synthetic", 0)

        self.assertEqual(vector["instruction_words"], [0x1010, 0xB600])
        self.assertFalse(vector["indexed_adapted"])
        self.assertEqual(vector["indexed"]["adapted_extension_word"], 0xB600)

    def test_nonindexed_vector_has_no_index_metadata(self):
        source = indexed_test(0x0000)
        source["name"] = "000 NOP 4e71"
        source["initial"]["prefetch"] = [0x4E71, 0x0000]
        source["final"] = copy.deepcopy(source["initial"])
        vector = adapter.normalize_test(source, "synthetic", 0)

        self.assertEqual(vector["opcode"], 0x4E71)
        self.assertEqual(vector["instruction_words"], [0x4E71, 0x0000])
        self.assertIsNone(vector["indexed"])

    def test_invalid_ram_byte_is_rejected(self):
        source = indexed_test()
        source["initial"]["ram"] = [[0x1000, 0x1000]]

        with self.assertRaises(adapter.CorpusError):
            adapter.normalize_test(source, "synthetic", 0)

    def test_missing_opcode_suffix_is_rejected(self):
        source = indexed_test()
        source["name"] = "000 MOVE.L d8(A0,D3),D0"

        with self.assertRaises(adapter.CorpusError):
            adapter.normalize_test(source, "synthetic", 0)


if __name__ == "__main__":
    unittest.main()
