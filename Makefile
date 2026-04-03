proj: main.o simulator.o
	g++ -O3 -o proj main.o simulator.o

main.o: main.cpp simulator.h
	g++ -O3 -c main.cpp

simulator.o: simulator.cpp simulator.h instruction.h
	g++ -O3 -c simulator.cpp

clean:
	rm -f *.o proj