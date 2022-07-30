APP ?= pengo #OR: pacman bombjack

#USE_SOKOL_DIRECT = -DUSE_SOKOL_DIRECT #this controls if using original sokol libs or not

INC += -I../common
INC += -I../../../sokol
INC += -I../../../sokol/util
INC += -I../../../chips
INC += -I../roms
INC += -I../../../sokol/tests/ext/
INC += -I../../../fips-build/chips-test/linux-make-release/examples/common/

CFLAGS += -O3
LIBS = -ldl -lX11 -lXcursor -lXi -lGL -lpthread -lm -lasound
LDFLAGS += $(LIBS)

all: $(APP).bin

$(APP).o: $(APP).c
	gcc $(CFLAGS) $(INC) -c $(APP).c -o $(APP).o

$(APP).bin: sokol_hal.c sokol_hal.c $(APP).o
	gcc  $(USE_SOKOL_DIRECT) $(CFLAGS) $(INC) -c sokol_hal.c -o sokol_hal.o
	gcc  $(USE_SOKOL_DIRECT) $(CFLAGS) $(INC) -c sokol_hal2.c -o sokol_hal2.o
	gcc sokol_hal.o sokol_hal2.o $(APP).o -o $(APP).bin $(LDFLAGS)

clean:
	rm -f *.bin *.o
	
run: all
	./$(APP).bin
