# contract tests run on any host — no board, no SDP needed
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -I.

.PHONY: test clean

test: test_contract
	./test_contract

test_contract: test_contract.c frame_v2.h
	gcc $(CFLAGS) test_contract.c -o $@

clean:
	rm -f test_contract
