COMPILE_OPTIONS = -std=c++11 -Wall -Wextra -Wformat=2 -Wformat-security -Wformat-signedness -Wold-style-cast -Wstrict-overflow -Wundef -Wlogical-op -Wcast-qual -Wconversion -Wsign-conversion -fstack-protector-strong --param=ssp-buffer-size=2 -pie -fPIE -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -D_FORTIFY_SOURCE=2 -O3 -g -march=native

all: graph

graph: graph.cpp
	g++ $(COMPILE_OPTIONS) graph.cpp -lglut -lGLU -lGL -lrt -lpthread -o graph
	chmod g-rwx,o-rwx graph

clean:
	rm graph

