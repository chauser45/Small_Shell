This application consists of a basic shell program developed in C which implements several basic commands such as exit,cd, and status as well as the ability to create new
processes in foreground or background. Simple input and output redirection away from standard I/O and custom signal handling are also included.


The procedure below was used during submission to run the program on a linux FLIP server run by the school.
Extract the zip to its own folder on os1. Run:
"make all"
The compiled program will be named "smallsh".
From the folder, run:
"smallsh" to start the shell

I was able to pass the script by first running:
chmod +x ./p3testscript

and then running:

./p3testscript > mytestresults 2>&1 
