#include <stdio.h>
#include <syscall.h>

int main(int argc, char **argv){
  if(argc >= 2){
	int i = 0, val = 0;
	while(argv[1][i]){
	  val *= 10;
	  val += argv[1][i]-'0';
	  i++;
	}
	printf("%d ", fibonacci(val));
  }
  if(argc == 5){
	int num[4];
	for(int i = 1; i < 5; i++){
	  int j = 0, val = 0;
	  while(argv[i][j]){
		val *= 10;
		val +=argv[i][j]-'0';
		j++;
	  }
	  num[i - 1] = val;
	}
	printf("%d", max_of_four_int(num[0], num[1], num[2], num[3]));
  }
  printf("\n");
  return EXIT_SUCCESS;
}
