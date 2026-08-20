Import("env")

from os.path import isfile, join


platform = env.PioPlatform()
framework_libs = platform.get_package_dir("framework-arduinoespressif32-libs")
model_image = join(framework_libs, "esp32s3", "esp_sr", "srmodels.bin")

if not isfile(model_image):
    raise RuntimeError(f"ESP-SR model image not found: {model_image}")

env.Append(FLASH_EXTRA_IMAGES=[("0xC10000", model_image)])
