import ctypes as c
import json
import os
import sys
from pathlib import Path

locale, expected_name, expected_window = sys.argv[1:4]
obs_bin = Path(os.environ["OBS_TEST_BIN"])
os.add_dll_directory(str(obs_bin))
obs = c.CDLL(str(obs_bin / "obs.dll"))
obs.obs_startup.argtypes = [c.c_char_p, c.c_char_p, c.c_void_p]
obs.obs_startup.restype = c.c_bool
obs.obs_open_module.argtypes = [c.POINTER(c.c_void_p), c.c_char_p, c.c_char_p]
obs.obs_open_module.restype = c.c_int
obs.obs_init_module.argtypes = [c.c_void_p]
obs.obs_init_module.restype = c.c_bool
for name in ("obs_get_module_name", "obs_get_module_version", "obs_get_module_id", "obs_get_module_author"):
    func = getattr(obs, name)
    func.argtypes = [c.c_void_p]
    func.restype = c.c_char_p
obs.obs_source_get_display_name.argtypes = [c.c_char_p]
obs.obs_source_get_display_name.restype = c.c_char_p
obs.obs_get_source_properties.argtypes = [c.c_char_p]
obs.obs_get_source_properties.restype = c.c_void_p
obs.obs_properties_get.argtypes = [c.c_void_p, c.c_char_p]
obs.obs_properties_get.restype = c.c_void_p
obs.obs_property_description.argtypes = [c.c_void_p]
obs.obs_property_description.restype = c.c_char_p
obs.obs_properties_destroy.argtypes = [c.c_void_p]

assert obs.obs_startup(locale.encode(), None, None)
module = c.c_void_p()
root = str(Path(__file__).resolve().parent.parent / "build/locale-install-test/obs-cursor-filter")
assert obs.obs_open_module(c.byref(module), os.path.join(root, "obs-cursor-filter.dll").encode(),
                           os.path.join(root, "data").encode()) == 0
manifest = json.loads((Path(root) / "data/manifest.json").read_text(encoding="utf-8"))
assert obs.obs_get_module_name(module).decode() == manifest["display_name"]
assert obs.obs_get_module_version(module).decode() == manifest["version"]
assert obs.obs_get_module_id(module).decode() == manifest["id"]
assert obs.obs_get_module_author(module).decode() == "@ropotama"
assert obs.obs_init_module(module)
assert obs.obs_source_get_display_name(b"cursor_filter").decode() == expected_name
props = obs.obs_get_source_properties(b"cursor_filter")
assert props
window = obs.obs_properties_get(props, b"window")
assert window and obs.obs_property_description(window).decode() == expected_window
obs.obs_properties_destroy(props)
obs.obs_shutdown()
print(f"{locale}: OK")
