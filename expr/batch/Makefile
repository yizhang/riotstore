LD_RUN_PATH = ../..
include ../../flags.mk
CXXFLAGS += -I../.. -I../../common -I../../btree -I../../lower -I../../directly_mapped -I/usr/local/include
LDFLAGS += $(shell pkg-config --libs gsl)
LDFLAGS += -L../.. -lriot_store

.PHONY: all clean lower

SRC :=  write.cpp gen.cpp testequal.cpp
OBJ := $(SRC:.cpp=.o)
EXE := $(SRC:.cpp=)

all: $(EXE)

-include $(OBJ:.o=.dd)

%:%.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean : 
	rm -f $(EXE)
	rm -f $(OBJ)
	rm -f $(OBJ:.o=.dd)
