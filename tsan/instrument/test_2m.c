

#include <stdio.h>
#include <stdlib.h>

int* gp_i;
int* gp_i2;

int* lpi;
int main() {
	lpi = malloc(100);
	gp_i = lpi + 10;
	int* lpi2 = lpi;
	*gp_i = 11111;
	free(lpi2);

	lpi2 = malloc(100);
	lpi = lpi2;

	gp_i2 = lpi + 11;

	*gp_i = 22222; // danger!
	*gp_i2 = 33333; // safe


	free(lpi2);
printf("lpi %p %p\n", &lpi, lpi);
printf("lpi2 %p %p\n", &lpi2, lpi2);
printf("gp_i %p %p\n", &gp_i, gp_i);
printf("gp_i2 %p %p\n", &gp_i2, gp_i2);



	return 0;
}