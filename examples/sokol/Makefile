APP ?= pengo #OR: pacman bombjack

#USE_SOKOL_DIRECT = -DUSE_SOKOL_DIRECT #this controls if using original sokol libs or not

INC += -I../common
INC += -I../../../sokol
INC += -I../../../sokol/util
INC += -I../../../chips
INC += -I../roms
INC += -I../../../sokol/tests/ext/
INC += -I../../../fips-build/chips-test/linux-make-release/examples/common/
INC += -I../../../fips-libmodplug/libmodplug/src

CFLAGS += -O3 $(shell pkg-config sdl2 --cflags)
LIBS = -ldl -lX11 -lXcursor -lXi -lGL -lpthread -lm -lasound $(shell pkg-config sdl2 --libs)
LIBS += $(shell pkg-config libmodplug --libs) #for modplug

LDFLAGS += $(LIBS)

all: $(APP).bin

$(APP).o: $(APP).c
	gcc $(CFLAGS) $(INC) -c $(APP).c -o $(APP).o

$(APP).bin: sokol_hal.c sokol_hal2.c sdl_fb.c $(APP).o
	gcc  $(USE_SOKOL_DIRECT) $(CFLAGS) $(INC) -c sokol_hal.c -o sokol_hal.o
	gcc  $(USE_SOKOL_DIRECT) $(CFLAGS) $(INC) -c sokol_hal2.c -o sokol_hal2.o
	gcc $(CFLAGS) sdl_fb.c sokol_hal.o sokol_hal2.o $(APP).o -o $(APP).bin $(LDFLAGS)

clean:
	rm -f *.bin *.o
	
run: all
	./$(APP).bin
