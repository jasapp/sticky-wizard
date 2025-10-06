# M5Dial Glue Gun Makefile
# Because fuck clicking around in GUIs

ARDUINO_CLI = /home/jasapp/bin/arduino-cli
SKETCH = sticky-wizard.ino
BOARD = esp32:esp32:m5stack_dial
PORT = /dev/ttyACM0

# Build settings
BUILD_DIR = build

.PHONY: all compile upload clean monitor list-boards list-ports help

all: compile

compile:
	@echo "==> Compiling $(SKETCH)..."
	$(ARDUINO_CLI) compile --fqbn $(BOARD) $(SKETCH)

upload: compile
	@echo "==> Uploading to M5Dial..."
	$(ARDUINO_CLI) upload -p $(PORT) --fqbn $(BOARD) $(SKETCH)

clean:
	@echo "==> Cleaning build files..."
	rm -rf $(BUILD_DIR)
	@echo "Done."

monitor:
	@echo "==> Opening serial monitor on $(PORT)..."
	$(ARDUINO_CLI) monitor -p $(PORT) -c baudrate=115200

flash: upload monitor

list-boards:
	@echo "==> Available boards:"
	$(ARDUINO_CLI) board listall esp32

list-ports:
	@echo "==> Available ports:"
	$(ARDUINO_CLI) board list

help:
	@echo "M5Dial Glue Gun - Makefile Commands"
	@echo ""
	@echo "  make          - Compile the sketch"
	@echo "  make compile  - Compile the sketch"
	@echo "  make upload   - Compile and upload to M5Dial"
	@echo "  make monitor  - Open serial monitor"
	@echo "  make flash    - Upload and start monitor"
	@echo "  make clean    - Clean build files"
	@echo "  make list-boards - Show available ESP32 boards"
	@echo "  make list-ports  - Show connected devices"
	@echo ""
	@echo "To use a different port: make upload PORT=/dev/ttyUSB0"
