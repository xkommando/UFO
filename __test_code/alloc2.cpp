


#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <iostream>
#include <vector>

using namespace std;


void test() {

	int* p1 = (int*)malloc(250);
	*(p1 + 30) = 5;
	cout << "malloc1 " << p1 << " acc1 "<< (p1 + 30) << endl;
	int* p2 = p1;
	free(p1);

	int* p3 = (int*)malloc(250);
	*(p3 + 50) = 99;
	cout << "malloc2 " << p3 << " acc2 "<< (p3 + 50) << endl;

	*p2 = 5;
	cout << " acc3 "<< (p2) << endl;
	free(p3);
	{
		int* p4 = (int*)malloc(1<<30);
		int* p5 = (int*)malloc(1<<30);
		cout << "malloc3 " << p4 << "  " << *p4 << endl; 
		free(p4);free(p5);
	}
}

int* gp1;
int* gp2;

void* t2(void* argp) {
	int r;
	cout << "test2 acc gp1 " << gp1 << "  " << *gp1 << endl; 
	gp2 = (int*)malloc(200);
	cout << "test2 malloc 2 " << gp2 << endl;
	sleep(1);
	free(gp2);
	sleep(1);
	return NULL;
}
void test2() {
	pthread_t thr2;
	gp1 = (int*) malloc(200);
	cout << "\r\n\r\ntest2 malloc 1 " << gp1 << endl;
	pthread_create(&thr2, NULL, &t2, NULL);  
	sleep(1);
	free(gp1);
	cout << "test2 acc gp2 " << gp2 << "  " << *gp2 << endl;
	 
	{
		int* p4 = (int*)malloc(1<<30);
		int* p5 = (int*)malloc(1<<30);
		*p4 = 5;
		free(p4);
		free(p5);
	}
	pthread_join(thr2, NULL);
}

int main() {
	cout << "\r\nstart!!!\r\n\r\n";
	test();
	test2();
	cout << "\r\nend!!!\r\n\r\n";
	return 0;
}


