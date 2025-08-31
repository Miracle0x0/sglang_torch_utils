.PHONY: script

script: sgl_utils.py
	@echo "Making executable uv script..."
	@rm -f sgl_utils
	@{ echo "#!/usr/bin/env -S uv run --script"; echo "#"; cat sgl_utils.py; } > sgl_utils
	@chmod +x sgl_utils

