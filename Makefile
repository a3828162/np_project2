all:	
	g++ -w np_simple.cpp -o np_simple
	g++ -w np_single_proc.cpp -o np_single_proc
	g++ -w np_multi_proc.cpp -o np_multi_proc -lrt

clean:
	rm np_simple
	rm np_single_proc
	rm np_multi_proc
