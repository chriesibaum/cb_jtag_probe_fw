TWISTER=../../zephyr/scripts/twister

MODULE_ROOT=../
HW_MAP?=./hardware-map.yml

TWISTER_OUT_APP_BUILD=./twister-out__app_build
TWISTER_OUT_TEST_PROBE=./twister-out__test_probe


.PHONY: list-tests twister_app_build twister_read_idcodes clean mr-proper



list-tests:
	@echo "Listing all available tests in the current directory using Twister..."
	$(TWISTER) -T  ./  --list-tests #\
# 		--device-testing --hardware-map $(HW_MAP)

all: twister_app_build twister_probe

twister_app_build:
	$(TWISTER) -T  ./test_app_build \
		--outdir $(TWISTER_OUT_APP_BUILD) \

	junit2html $(TWISTER_OUT_APP_BUILD)/twister_report.xml

twister_probe:
	@echo "Running test_probe.pytest using Twister..."
	$(TWISTER) -T ./ -s cb_jtag_probe.test_probe.pytest \
		-p rpi_pico2/rp2350a/m33 \
		--device-testing \
		--hardware-map $(HW_MAP) \
		--outdir $(TWISTER_OUT_TEST_PROBE)

	junit2html $(TWISTER_OUT_TEST_PROBE)/twister_report.xml


clean:
	@echo "Cleaning build artifacts and test output directories..."
	rm -rf ./twister-out*
	rm -rf ./.pytest_cache/
	rm -rf ./*/__pycache__/


mr-proper: clean
	@echo "Performing a deep clean, including build artifacts in the app directory..."
	make -C ../app clean