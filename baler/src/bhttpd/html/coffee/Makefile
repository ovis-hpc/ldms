
COFFEE_FILES = $(wildcard *.coffee)
JS_DIR = ../js
JS_FILES = $(COFFEE_FILES:%.coffee=$(JS_DIR)/%.js)

all: $(JS_FILES)

$(JS_DIR)/%.js: %.coffee
	coffee -c -o $(JS_DIR) $<

clean:
	rm -f $(JS_FILES)
