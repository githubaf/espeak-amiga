#include <stdio.h>

int main(void)
{
	for (;;)
	{
		int i=0;
		
		printf("i=%d\n",i);
		i++;

		if(i==10)
			break;
	}

	return 0;
}
