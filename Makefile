# MetaRecoverX offline C++ build (Ubuntu)

.PHONY: all build clean

all: build

build:
	./scripts/build.sh
	@echo "Binaries:"
	@echo "  build/xfs_scan"
	@echo "  build/btrfs_scan"
	@echo "  build/reconstruct_csv"
	@echo "  build/recover_csv"

clean:
	rm -rf build artifacts recovered report.csv
