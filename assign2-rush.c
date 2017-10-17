#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>

#define CROSS_TIME 1
#define DIREC_PROB 0.6
#define NUM_LANES 2
#define BRIDGE_CAP 5

#define handle_err(s) do{fprintf(stderr, "%s", s); exit(EXIT_FAILURE);}while(0)

//Cody Burchett
//cburchet	

typedef enum {
  EAST,
  WEST,
} direc_t;

typedef struct _thread_argv
{
  int cid;
  int direc;
  int xtime;
} thread_argv;

/**
 * Student may add necessary variables to the struct
 **/
typedef struct _lane {
  direc_t direc;
  int curr_num_cars;
  int carsInLaneCrossingBridge;
} lane_t;

typedef struct _bridge {
  lane_t lanes[NUM_LANES];
  int total_num_cars;
  int capacity;
} bridge_t;

void bridge_init();
void bridge_destroy();
void dispatch(char *str, int num_cars);
void print_status();
void *OneVehicle(void *argv);

void ArriveBridge(int vid, direc_t d);
void CrossBridge(int vid, direc_t , int xtime);
void ExitBridge(int vid, direc_t );

int IsThisCarInWestLane(int vid);
int AreThereEastCarsInWestLane();
void AddToRushHourArray(int vid);

extern int errno;

pthread_t *threads = NULL;	/* Array to hold thread structs */
bridge_t br;			/* Bridge struct shared by the vehicle threads*/

int departure_index = 0;	
int isWestLaneUsedByEast = 0;
int isWestLaneUsedByWest = 0;
int vidsOfEastCarInWestLane[4] = {-1, -1, -1, -1};

//list of ids that detail which car should go next for each direction
int* westVehicleIdQueue;
int* eastVehicleIdQueue;	
int currentEastQueuePos = 0;
int currentWestQueuePos = 0;
int nextEastQueuePos = 0;
int nextWestQueuePos = 0;

pthread_mutex_t trafficControl = PTHREAD_MUTEX_INITIALIZER; 
pthread_cond_t east_enter_wait = PTHREAD_COND_INITIALIZER;
pthread_cond_t west_enter_wait = PTHREAD_COND_INITIALIZER;

/**
 *	NUM_CARS is required.
 *	If GEN_STR is provided, cars will be dispatched according to it.
 *	Otherwise, cars will be randomly generated.
 *
 *	Car creation string
 *	Example: 
 *	$ ./assign2 12 EEEWWWWWP10EWEW
 *	E means a car is going EAST
 *	W means a car is going WEST
 *	P10 means pausing car arrival for 10 seconds
 *	In total there will be 12 cars
 */
int main(int argc, char *argv[])
{
  int num_cars, i;
  char *gen_str = NULL;
  
  if (argc < 2)
    {
      printf("Usage: %s NUM_CARS [GEN_STR]\n", argv[0]);
      exit(EXIT_SUCCESS);
    }
  
  srand(time(NULL));
  
  /* Process Arguments */
  num_cars = atoi(argv[1]);
  //allocate enough space for the east and west waiting queues
  eastVehicleIdQueue = (int*)malloc(sizeof(int) * num_cars);
  westVehicleIdQueue = (int*)malloc(sizeof(int) * num_cars);
  if (argc == 3)
    gen_str = argv[2];
  
  /* Init bridge struct */
  bridge_init();
  
  dispatch(gen_str, num_cars);
  
  /* Join all the threads */
  for (i = 0; i < num_cars; i++)
    pthread_join(threads[i], NULL);
  
  /* Clean up and exit */
  bridge_destroy();
  
  exit(EXIT_SUCCESS);
}

/**
 *	If str is given, i.e. not NULL, car threads will be created according
 *	to the string. Otherwise, car will be created randomly based
 *	on DIREC_PROB. In both cases, the second argument num_cars needs to
 *	set correctly.
 **/
void dispatch(char *str, int num_cars)
{
  int i = 0;
  direc_t d;
  
  fprintf(stderr, "Dispatching %d vehicles\n", num_cars);
  
  /* Allocate memory for thread structs */
  if ((threads = (pthread_t *)malloc(sizeof(pthread_t) * num_cars)) == NULL)
    handle_err("malloc() Failed for threads");
  
  if (str != NULL)
    {
      int cid = 0, st, sp;
      while (str[i] != '\0')
	{
	  switch (str[i])
	    {
	    case 'P':
	      st = sp = i + 1;
	      while (str[sp] != '\0' && isdigit(str[sp]))
		sp++;
	      fprintf(stderr, "Dispatcher Sleep for %d seconds...\n", atoi(str + st));
	      sleep(atoi(str + st));
	      fprintf(stderr, "Dispatcher restarts...\n");
	      i = sp;
	      continue;
	    case 'E':
	      d = EAST; break;
	    case 'W':
	      d = WEST; break;
	    default:
	      handle_err("Unknown character in dispatch string...abort\n");
	    }
	  
	  thread_argv *args = (thread_argv *) malloc (sizeof(thread_argv));
	  *args = (thread_argv) {cid, d, CROSS_TIME};
	  if (pthread_create(threads + cid++, NULL, &OneVehicle, args) != 0)
	    handle_err("pthread_create Failed");
	  
	  ++i;
	}
    }
  else
    for (i = 0; i < num_cars; ++i)
      {
	/* The probability of direction EAST is DIREC_PROB */
	direc_t d = rand() % 1000 > DIREC_PROB * 1000 ? EAST : WEST;
	
	thread_argv *args = (thread_argv *) malloc (sizeof(thread_argv));
	*args = (thread_argv) {i, d, CROSS_TIME};
	if (pthread_create(threads + i, NULL, &OneVehicle, args) != 0)
	  handle_err("pthread_create Failed");
      }
}

void *OneVehicle(void *argv)
{
  thread_argv *args = (thread_argv *)argv;
  
  ArriveBridge(args->cid, args->direc);
  //print_status();
  CrossBridge(args->cid, args->direc, args->xtime);
  //print_status();
  ExitBridge(args->cid, args->direc);
  //print_status();

  free(args);
  
  pthread_exit(0);
}

/* Students can add code to capture the state of the queues at the two ends of the bridge; Also take care to ensure that any shared memory accesses are within a critical section
*/

void print_status()
{
	pthread_mutex_lock(&trafficControl);
  // fprintf(stderr, "[Bridge] %2d / %2d [Waiting East] %2d [Crossing East] %2d [Waiting West] %2d [Crossing West] %2d [WestUsedByEast] %s\n", 
	  // br.total_num_cars, br.capacity,
	  // br.lanes[0].curr_num_cars,
	  // br.lanes[0].carsInLaneCrossingBridge,
	  // br.lanes[1].curr_num_cars,
	  // br.lanes[1].carsInLaneCrossingBridge,
	  // isWestLaneUsedByEast ? "true" : "false"
	  // );
	  pthread_mutex_unlock(&trafficControl);
}

void bridge_init()
{
  int i;
  for (i = 0; i < NUM_LANES; i++)
    br.lanes[i] = (lane_t) {i % 2 ? EAST : WEST, 0, 0};
  br.capacity = BRIDGE_CAP;
  br.total_num_cars = 0;
  return;
}

void bridge_destroy()
{
  free(threads);
  return;
}
//east traffic can use west lane in this version
void ArriveBridge(int vid, direc_t d)
{
	pthread_mutex_lock(&trafficControl);
	fprintf(stderr, "Car %3d arrives going %s\n", vid, d == EAST ? "EAST" : "WEST");
	//keep track of whether there are cars waiting in each direction
	if (d==EAST)
		br.lanes[0].curr_num_cars++;
	else
	{
		br.lanes[1].curr_num_cars++;
	}
	
	//wait cars while bridge is full
	while(br.total_num_cars == br.capacity || (d==WEST && isWestLaneUsedByEast))
	{
		if (d == EAST)
		{
			//add vehicle id to list of waiting eastbound ids
			eastVehicleIdQueue[currentEastQueuePos] = vid;
			currentEastQueuePos++;
			//ensure only the car that was first to arrive crosses next
			do{
				pthread_cond_wait(&east_enter_wait, &trafficControl);
			}while(eastVehicleIdQueue[nextEastQueuePos] != vid);
			nextEastQueuePos++;
		}
		else if (d == WEST)
		{
			//add vehicle id to list of waiting westbound ids
			westVehicleIdQueue[currentWestQueuePos] = vid;
			currentWestQueuePos++;
			//ensure only the car that was first to arrive crosses next
			do{
				pthread_cond_wait(&west_enter_wait, &trafficControl);
			}while(westVehicleIdQueue[nextWestQueuePos] != vid);
			nextWestQueuePos++;
		}
	}
	br.total_num_cars++;
	
	if (d == EAST)
	{
		br.lanes[0].curr_num_cars--;
		if (!isWestLaneUsedByWest)
		{
			//send east car in east lane
			if (br.lanes[1].carsInLaneCrossingBridge >= br.lanes[0].carsInLaneCrossingBridge)
			{
				br.lanes[0].carsInLaneCrossingBridge++;
				//determine if east-bound cars are in west lane
				if (br.lanes[1].carsInLaneCrossingBridge > 0)
				{
					isWestLaneUsedByEast = 1;
				}
				else
				{
					isWestLaneUsedByEast = 0;
				}
			}
			//send east car in west lane
			else
			{
				br.lanes[1].carsInLaneCrossingBridge++;
				isWestLaneUsedByEast = 1;
				AddToRushHourArray(vid);
			}
		}
		else
		{
			br.lanes[0].carsInLaneCrossingBridge++;
			isWestLaneUsedByEast = 0;
		}
	}
	else
	{
		br.lanes[1].curr_num_cars--;
		isWestLaneUsedByWest = 1;
		br.lanes[1].carsInLaneCrossingBridge++;
	}
	pthread_mutex_unlock(&trafficControl);
  return;
}

void CrossBridge(int vid, direc_t d, int xtime)
{
  fprintf(stderr, "Car %3d crossing bridge going %s\n", vid, d == EAST ? "EAST" : "WEST");
  sleep(xtime);
  return;
}

void ExitBridge(int vid, direc_t d)
{
	pthread_mutex_lock(&trafficControl);
  br.total_num_cars--;
  departure_index++;
  fprintf(stderr, "Car %3d exits the bridge and west lane used by east = %s\n", vid, isWestLaneUsedByEast ? "true":"false");
  //handle exiting cars moving east
  if (d == EAST)
  {
	  //check if west lane is in use by east-bound cars
	  if (isWestLaneUsedByEast)
	  {
		  //check if this east-bound vid is in west lane
		  if (IsThisCarInWestLane(vid))
		  {
			  br.lanes[1].carsInLaneCrossingBridge--;
			  //check if this was the last east-bound car in west lane
			  if (!AreThereEastCarsInWestLane())
			  {
				  isWestLaneUsedByEast = 0;
				  isWestLaneUsedByWest = 0;
				  //signal east bound car if no cars waiting to travel west
				  if (br.lanes[1].curr_num_cars == 0)
				  {
					  pthread_cond_broadcast(&east_enter_wait);
				  }
				  //signal west bound car if there is one waiting
				  else
				  {
					  pthread_cond_broadcast(&west_enter_wait);
				  }
			  }
			  //west bound lane is in use by east-bound cars, signal east bound car
			  else
			  {
				  pthread_cond_broadcast(&east_enter_wait);
			  }
		  }
		  //this car is not in west lane, but west lane is used by east-bound cars, signal new east-bound car to cross
		  else
		  {
			  br.lanes[0].carsInLaneCrossingBridge--;
			  pthread_cond_broadcast(&east_enter_wait);
		  }
	  }
	  //west lane is not being used by east-bound cars signal east-bound car unless none are waiting
	  else
	  {
		  br.lanes[0].carsInLaneCrossingBridge--;
		  if (br.lanes[0].curr_num_cars == 0)
		  {
			  pthread_cond_broadcast(&west_enter_wait);
		  }
		  else
		  {
		  	pthread_cond_broadcast(&east_enter_wait);
		  }
	  }
  }
  //car is west bound
  else
  {
	br.lanes[1].carsInLaneCrossingBridge--;
	//signal car waiting to travel west if there is one otherwise east
	if (br.lanes[1].curr_num_cars == 0)
	{
		if (br.lanes[1].carsInLaneCrossingBridge == 0)
		{
			isWestLaneUsedByWest = 0;
		}
		pthread_cond_broadcast(&east_enter_wait);
	}
	else
	{
		pthread_cond_broadcast(&west_enter_wait);
	}
  }
  //debugging print helpful to determine when west lane has been unblock by east-bound cars
  fprintf(stderr, "west lane has been unblocked by east cars %s\n", isWestLaneUsedByEast ? "false": "true");
  pthread_mutex_unlock(&trafficControl);
  return;
}

//check if car is in west lane (only used to keep track of east-bound cars in west lane)
int IsThisCarInWestLane(int vid)
{
	int i;
	for (i = 0; i < 4; i++)
	{
		if (vidsOfEastCarInWestLane[i] == vid)
		{
			vidsOfEastCarInWestLane[i] = -1;
			return 1;
		}
	}
	return 0;
}

//check if there are any remaining east-bound cars in west lane
int AreThereEastCarsInWestLane()
{
	int i;
	for (i = 0; i < 4; i++)
	{
		if (vidsOfEastCarInWestLane[i] != -1)
		{
			return 1;
		}
	}
	return 0;
}

void AddToRushHourArray(int vid)
{
	int i;
	for (i = 0; i < 4; i++)
	{
		if (vidsOfEastCarInWestLane[i] == -1)
		{
			vidsOfEastCarInWestLane[i] = vid;
			return;
		}
	}
	return;
}