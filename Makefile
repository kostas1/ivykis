test:		test.c kruva.c kruva_local.c kruva_netns.c kruva_util.c
		gcc -Wall -O6 -g -o test test.c kruva.c kruva_local.c kruva_netns.c kruva_util.c -livykis
