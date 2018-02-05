/* 
Check whether fopen() kills stdin (bug in some libnix!) or not
AF, Gwd 16.Sep.2017

On Amiga 
echo "Hello" >hello.txt
fopen_test <hello.txt  ;should print "fgets() delivered Hello" 

or in linux-shell or Amiga ADE sh
echo "Hello" | vamos ./fopen_test

returns 0 in case of success.
*/

#include <stdio.h>
#include <string.h>

#define DUMMYFILE "dummy.txt"

int main (void)
{
	char Buffer[256];
	char *FgetsResult;
	int Ret=-1;
	FILE *DummyFile=fopen(DUMMYFILE,"w");   /* This kills stdin in some versions of libnix */ 
	
	if(DummyFile)
	{
		FgetsResult=fgets(Buffer,sizeof(Buffer),stdin);
		if(FgetsResult)
		{
			fputs("fgets() delivered ",stdout);
			fputs(Buffer,stdout);
			if(!strncmp("Hello",Buffer,strlen("Hello")))
			{
				fputs("Correct.\n",stdout);
				Ret=0;
			}
			else
			{
				fputs("Wrong!!!\n",stdout);
				Ret=3;
			}
		}
		else
		{
			fputs("fgets() returned NULL\n",stdout);
			fputs("Wrong!!!\n",stdout);
			Ret=2;
		}

		fclose(DummyFile);
	}
	else
	{
		fputs("Could not open " DUMMYFILE "\n",stdout);
                fputs("Wrong!!!\n",stdout);
		fputs("get\n",stdout);
		Ret=1;
	}
	return Ret;
}

