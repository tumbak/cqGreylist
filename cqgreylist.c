/*
#
# Version 0.2
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
*/


#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

/*
 * Change anything you want here
 */
/* RFC 2821 specifies the timeout for recieving a command to at least 5 mins */
#define TIMEOUT	300
/* specify the greylisting time in which to not accept mail from a sender */
#define GREY_SECONDS	60

char* hostname = "your.fqdn.com";
char* message  = "you are greylisted try again";
char* base_directory = "/var/qmail/cqgreylist/";
/*
 * End of user editable parameters
 */


/* 512 chars according to RFC 2821 section 4.5.3 */
#define BUFFER_SIZE 512

#define	FALSE	0
#define	TRUE	1


void	first_octet( char* ip, char** octet );
int		is_greylisted( char* ip );
int		is_backtoosoon( char* ip );
void	negotiate_and_reject( char* message );
void	hand_to_smtpd( char* smtpd_line );
void	create_file( char* ip );
void	clean_exit( int retval );
void	parse_args( int argc, char* argv[], char** cmd_line );
void	timeout( int sig_num );


char* smtpd_line	= NULL;
char* buffer		= NULL;
char* cmd			= NULL;



int
main( int argc, char* argv[] ){

	char* remote_ip 	= NULL;
	/* get the environment variables */
	remote_ip	= getenv( "TCPREMOTEIP" );

	/*
	 * sanity check, remove if you badly need performance
	 */
	if( remote_ip == NULL ){
		/* this shouldnt happen */
		fprintf( stderr, "environment variable TCPREMOTEIP not set - exiting, " );
		clean_exit( -1 );
	}

	parse_args( argc, argv, &smtpd_line );

	char* relay_client	= NULL;
	char* whitelisted	= NULL;

	relay_client = getenv("RELAYCLIENT");
	whitelisted  = getenv("WHITELISTED");

	if( (relay_client != NULL) || (whitelisted != NULL) ){
#ifdef DEBUG
		fprintf( stderr, "accepted mail for relay client or it is whitelisted, " );
#endif
		/* if its a relay client then we handle to smtpd and exit cleanly */
		hand_to_smtpd( smtpd_line );
	}


	if( is_greylisted( remote_ip ) == TRUE ){
		if( is_backtoosoon( remote_ip ) == TRUE )
			negotiate_and_reject( message );
		else
			hand_to_smtpd( smtpd_line );
	}
	else{
		create_file( remote_ip );
		negotiate_and_reject( message );
	}


	return -1;

}


int
is_greylisted( char* ip ){

	char* octet		= NULL;
	int   retval	= FALSE;

	first_octet( ip, &octet );
	
	/* check to see if a directory exists */
	char *dir_name = NULL;
	int dir_name_size = strlen(base_directory) + strlen(octet);
	dir_name = malloc ((dir_name_size + 1) * sizeof(char));
	memset (dir_name, 0x0, dir_name_size + 1);
	strcat (dir_name, base_directory);
	strncat (dir_name, octet, dir_name_size - strlen(base_directory) );
	strcat (dir_name, "\0");

	DIR* dir_handler = NULL;

	dir_handler = opendir( dir_name );
	if( dir_handler == NULL ){
		if( errno == ENOENT ){	/* directory does not exist, create it */
			if( 0 == mkdir( dir_name, 0700 ) ){
#ifdef DEBUG
				fprintf( stderr, "Created directory %s , ", dir_name );
#endif
			}
			else{
				fprintf( stderr, "Could not create directory %s - deferring till you correct this, ", dir_name );
			}
		}
		else{
			/* TODO: handle other errors here */
			fprintf( stderr, "permissions or other errors for %s - deferring till you correct this, ", dir_name );
		}
	}
	else{
#ifdef DEBUG
		fprintf( stderr, "Directory %s already exists - opened, ", dir_name );
#endif
		closedir( dir_handler );
		retval = TRUE;
	}


/* if its still FALSE then an error happened up and we don't need to check for a file */
	if( retval != FALSE ){
		/* check if the file exists */
		struct stat file_stat;
		memset( &file_stat, 0, sizeof(file_stat) );

		char* file_name	= NULL;
		int file_name_size = dir_name_size + 1 /*"/"*/ + strlen(ip);
		file_name = malloc ((file_name_size + 1) * sizeof(char));
		memset (file_name, 0x0, file_name_size + 1);
		strcat (file_name, dir_name);
		strcat (file_name, "/");
		strncat (file_name, ip, file_name_size - dir_name_size - 1 );
		strcat (file_name, "\0");


		if( stat( file_name, &file_stat ) == 0 ){
			/* this host is grey listed, continue and return TRUE */
			retval = TRUE;
		}
		else{		/* couldnt stat the file */
#ifdef DEBUG
			fprintf( stderr, "Could not stat file %s , ", file_name );
#endif
			retval = FALSE;
		}

		if ( file_name ) free( file_name );
	}
		
	/* free */
	if ( octet ) free( octet );
	if ( dir_name ) free( dir_name );

	return retval;
}


int
is_backtoosoon( char* ip ){

	char*	octet	= NULL;
	int		retval	= TRUE;
	

	first_octet( ip, &octet );

	/* check if the file exists */
	struct stat file_stat;
	memset( &file_stat, 0, sizeof(file_stat) );
	int dir_name_size = strlen(base_directory) + strlen(octet);
	int file_name_size = dir_name_size + 1 /*"/"*/ + strlen(ip);

	char* file_name	= NULL;
	file_name = malloc ((file_name_size + 1) * sizeof(char));
	memset (file_name, 0x0, file_name_size + 1);
	strcat (file_name, base_directory);
	strncat (file_name, octet, dir_name_size - strlen(base_directory) );
	strcat (file_name, "/");
	strncat (file_name, ip, file_name_size - dir_name_size - 1 );
	strcat (file_name, "\0");

	if( stat( file_name, &file_stat ) == 0 ){
		/* file exists, do our stuff */
		if( difftime( time(NULL), file_stat.st_mtime ) > GREY_SECONDS ){
			/* been trying for the specified time, let him through */
#ifdef DEBUG
			fprintf( stderr, "Allowing %s , ", ip );
#endif
			retval = FALSE;
		}
		else{	/* you came back too soon */
#ifdef DEBUG
			fprintf( stderr, "%s Back too soon , ", ip );
#endif
		}
	}
	else{	/* couldnt stat the file, highly unlikely */
		fprintf( stderr, "Could not stat file %s - I think you should check it out, ", file_name );
		/* create the file */
	}

	/* free */
	if ( octet ) free( octet );
	if ( file_name ) free( file_name );

	return retval;
}

void
parse_args( int argc, char* argv[], char** line ){

	int length	= 0;
	int i 		= 0;

	for( i=1; i<argc; i++ ){
		length += strlen( argv[i] );
	}

	*line = malloc( (length + argc/*For the number of spaces*/ + 1)  * sizeof(char));
	memset( *line, 0, length + argc/*For the number of spaces*/ + 1 );

	for( i=1; i<argc; i++ ){
		strcat( *line, argv[i] );
		strcat( *line, " " );
	}

	return;
}


void
negotiate_and_reject( char* message ){

#ifdef DEBUG
	fprintf( stderr, "starting negotiation, " );
#endif

	/* set the timeout signal handler */
	signal( SIGALRM, timeout );

	typedef struct
	{
		char name[5];
		char response[64];
	}command;

	command commands[11] =
	{
		{	"HELO" , "" },
		{	"EHLO" , "250 hello\r\n"}, 
		{	"MAIL" , "250 ok\r\n"},
		{	"RCPT" , ""},
		{	"DATA" , ""},
		{	"RSET" , "250 ready\r\n"},
		{	"VRFY" , "502 not implemented\r\n"},
		{	"EXPN" , "502 not implemented\r\n"},
		{	"HELP" , "502 not implemented\r\n"},
		{	"NOOP" , "250 noop\r\n"},
		{	"QUIT" , ""}
	};

	snprintf( commands[0].response, sizeof(commands[0].response), "250 %s\r\n", hostname );
	commands[0].response[sizeof(commands[0].response)-1] = 0;

	snprintf( commands[3].response, sizeof(commands[3].response), "450 Rcpt to <> - %s\r\n", message );
	commands[3].response[sizeof(commands[3].response)-1] = 0;

	snprintf( commands[4].response, sizeof(commands[4].response), "451 %s\r\n", message );
	commands[4].response[sizeof(commands[4].response)-1] = 0;

	snprintf( commands[10].response, sizeof(commands[10].response), "221 %s Bye\r\n", hostname );
	commands[10].response[sizeof(commands[10].response)-1] = 0;

	buffer = malloc( BUFFER_SIZE + 1 * sizeof (char));
	memset( buffer, 0, BUFFER_SIZE + 1);
	
	cmd = malloc( 5 * sizeof(char));
	memset( cmd, 0, 5 );
	
	/* send the greeting */
	printf( "220 %s cqgreylist - reconnect later\r\n", hostname );
	fflush( NULL );

	int break_loop	= FALSE;
	int matched		= FALSE;

	alarm( TIMEOUT );		/* send us an alarm after the time specified */
	while( (fgets( buffer, BUFFER_SIZE, stdin ) != NULL) && break_loop != TRUE ){
		alarm( 0 );	
	
		matched = FALSE;
		/* extract the cmd */
		memset( cmd, 0, sizeof(cmd) );
		memcpy( cmd, buffer, 4 );

		if( strncasecmp( cmd, "QUIT" , sizeof(cmd) ) == 0 ){
			printf( "%s", commands[10].response );
			fflush( NULL );
			break_loop = TRUE;
			matched = TRUE;
			break;
		}

		int i = 0;
		for( i=0; i<10 && matched == FALSE; i++){
			if( strncasecmp( cmd, commands[i].name, sizeof(cmd) ) == 0 ){
				printf( "%s", commands[i].response );
				fflush( NULL );
				matched = TRUE;
				break;
			}
		}	/* end of for loop */
		if( matched == FALSE ){
			printf( "502 not implemented\r\n" );
			fflush( NULL );
		}
		alarm( TIMEOUT );
	}	/* end of while loop */

	/* free */
	if ( cmd ) free( cmd );
	if ( buffer ) free( buffer );

	clean_exit( 0 );
	
	return;
}


void
hand_to_smtpd( char* smtpd_line ){

#ifdef DEBUG
	fprintf( stderr, "executing %s , ", smtpd_line );
#endif

	if( system( smtpd_line ) == -1 ){
		fprintf( stderr, "could not execute next stage %s , ", smtpd_line );
	}

	/* nothing else to do, exit program */
	clean_exit( 0 );
	
	return;
}


void
create_file( char* ip ){

	char*	octet	= NULL;
	
	first_octet( ip, &octet );

	int dir_name_size = strlen(base_directory) + strlen(octet);
	int file_name_size = dir_name_size + 1 /*"/"*/ + strlen(ip);

	char* file_name	= NULL;
	file_name = malloc ((file_name_size + 1) * sizeof(char));
	memset (file_name, 0x0, file_name_size + 1);
	strcat (file_name, base_directory);
	strncat (file_name, octet, dir_name_size - strlen(base_directory) );
	strcat (file_name, "/");
	strncat (file_name, ip, file_name_size - dir_name_size - 1 );
	strcat (file_name, "\0");

	FILE* fh = NULL;

	fh = fopen( file_name, "w" );
	if( fh == NULL ){
		fprintf( stderr, "could not create file %s - this is bad, ", file_name );
	}
	else{
#ifdef DEBUG
		fprintf( stderr, "created file %s , ", file_name );
#endif
		fclose( fh );
	}
		
	/* free */
	if ( octet ) free( octet );
	if (file_name ) free( file_name );

	return;
}

void
first_octet( char* ip, char** pointer ){

	struct in_addr bin_ip;
	memset( &bin_ip, 0, sizeof(bin_ip) );

	/* TODO: need to check for bad IP format */
	inet_aton( ip, &bin_ip );

	*pointer = malloc( 4 * sizeof(char));	/* the string can never be longer than 4 */
	memset( *pointer, 0, 4 );

	sprintf( *pointer, "%d", 0x000000FF & bin_ip.s_addr );

	/* just to make sure we don't overflow */
	//pointer[3] = 0;

	return;
}

void
clean_exit( int retval ){

	/* free */
	if ( smtpd_line ) free( smtpd_line );

	exit( retval );
}

void
timeout( int sig_num ){

	printf( "421 %s timeout\r\n", hostname );

	/* free */
	if ( buffer ) free( buffer );
	if ( cmd ) free( cmd );

	clean_exit( -1 );
	return;
}
