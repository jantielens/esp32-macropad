import argparse
import pathlib
import subprocess
import unittest


parser = argparse.ArgumentParser()
parser.add_argument("--compiler", default="c++")
arguments, unittest_arguments = parser.parse_known_args()
root = pathlib.Path(__file__).resolve().parents[1]


class IdentifierGuards(unittest.TestCase):
    def compile(self, source, mcp):
        command = [
            arguments.compiler, "-std=c++17", "-fsyntax-only", "-x", "c++", "-",
            "-I" + str(root / "tests"), "-I" + str(root / "src/app"),
            "-I" + str(pathlib.Path.home() / "Arduino/libraries/ArduinoJson/src"),
            "-I" + str(pathlib.Path.home() / "Arduino/libraries/lvgl/src"),
            "-include", str(root / "tests/Arduino.h"),
            "-include", str(root / "tests/board_config.h"),
            "-DLV_CONF_SKIP=1", "-DHAS_MCP=" + str(mcp),
        ]
        command.extend("-DLV_FONT_MONTSERRAT_" + str(size) + "=1"
                       for size in (12, 18, 24, 32, 36, 48))
        return subprocess.run(command, input=source, text=True, capture_output=True)

    def test_widget_variants(self):
        variants = {
            "REGISTER_WIDGET": "nullptr, false",
            "REGISTER_WIDGET_SCHEMA": "nullptr, false",
            "REGISTER_WIDGET_SCHEMA_LIFECYCLE": "nullptr, false",
            "REGISTER_WIDGET_SCHEMA_VALIDATED_LIFECYCLE": "nullptr, false, nullptr",
        }
        for mcp in (0, 1):
            for macro, parameters in variants.items():
                for name in ("a" * 14, "a" * 15, "a" * 16, "gamepad_joystick"):
                    with self.subTest(mcp=mcp, macro=macro, name=name):
                        hooks = "\n".join(
                            f"constexpr auto {name}_{hook} = nullptr;"
                            for hook in ("parse", "create", "update", "destroy", "tick",
                                         "describe", "show", "hide")
                        )
                        source = (
                            '#include "widgets/widget.h"\n' + hooks + "\n"
                            f'const WidgetPreview {name}_preview = {{"Test", "test"}};\n'
                            f"{macro}({name}, {parameters});\n"
                        )
                        result = self.compile(source, mcp)
                        if len(name) <= 15:
                            self.assertEqual(result.returncode, 0, result.stderr)
                        else:
                            self.assertNotEqual(result.returncode, 0)
                            self.assertIn("Widget identifier exceeds CONFIG_WIDGET_TYPE_MAX_LEN", result.stderr)

    def test_persisted_action_name(self):
        for mcp in (0, 1):
            for name in ("a" * 14, "a" * 15, "a" * 16, "gamepad_joystick"):
                with self.subTest(mcp=mcp, name=name):
                    result = self.compile(
                        '#include "action_registry.h"\n'
                        f'DEFINE_AND_REGISTER_ACTION_TYPE(unrelated_symbol, "{name}", '
                        "nullptr, nullptr, nullptr, nullptr, nullptr);\n", mcp)
                    if len(name) <= 15:
                        self.assertEqual(result.returncode, 0, result.stderr)
                    else:
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn("Action identifier exceeds CONFIG_ACTION_TYPE_MAX_LEN", result.stderr)

    def test_pointer_is_not_a_literal_capacity(self):
        result = self.compile(
            '#include "action_registry.h"\n'
            'const char* name = "gamepad_joystick";\n'
            "DEFINE_AND_REGISTER_ACTION_TYPE(pointer_type, name, "
            "nullptr, nullptr, nullptr, nullptr, nullptr);\n", 0)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("action_type_name_fits", result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[__file__] + unittest_arguments)