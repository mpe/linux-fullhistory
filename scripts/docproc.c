#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/*
 *	A simple filter for the templates
 */

int main(int argc, char *argv[])
{
	char buf[1024];
	char *vec[1024];
	char type[64];
	int i;
	int vp=2;
	pid_t pid;


	if(chdir(getenv("TOPDIR")))
	{
		perror("chdir");
		exit(1);
	}
	
	/*
	 *	Build the exec array ahead of time.
	 */
	vec[0]="kernel-doc";
	vec[1]="-docbook";
	for(i=1;vp<1021;i++)
	{
		if(argv[i]==NULL)
			break;
		vec[vp++]=type;
		vec[vp++]=argv[i];
	}
	vec[vp++]=buf+2;
	vec[vp++]=NULL;
	
	/*
	 *	Now process the template
	 */
	 
	while(fgets(buf, 1024, stdin))
	{
		if(*buf!='!')
			printf("%s", buf);
		else
		{
			fflush(stdout);
			if(buf[1]=='E')
				strcpy(type, "-function");
			else if(buf[1]=='I')
				strcpy(type, "-nofunction");	
			else
			{
				fprintf(stderr, "Unknown ! escape.\n");
				exit(1);
			}
			switch(pid=fork())
			{
				case -1:
					perror("fork");
					exit(1);
				case  0:
					execvp("scripts/kernel-doc", vec);
					perror("exec");
					exit(1);
				default:
					waitpid(pid, NULL,0);
			}
		}
	}
	exit(0);
}
