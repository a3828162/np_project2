all:	
	g++ np_single_proc.cpp -o np_single_proc
#	g++ np_multi_proc.cpp -o np_multi_proc -lrt

clean:
	rm np_single_proc
#   rm np_multi_proc