OBJS=main.o gdbremote.o core.o sci.o
BIN=sim

$(BIN): $(OBJS)
	$(CC) -o $(BIN) $(OBJS)

%.o:%.c
	$(CC) -c -g -o $@ $<

.PHONY: clean
clean:
	$(RM) $(BIN) $(OBJS)
