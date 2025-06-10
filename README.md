# CS4760-P6
UMSL Spring 2025 Operating Systems 
Project 6: Paging
# Description
This program builds off of Project 1-5  shifting the focus from
resource management to memory management via paging. The project retains 
the majority of its functionality but will be closely monitoring memory 
frames and pages to be allocated among worker processes launched by oss.
# Paging Algorithm
Least Recently Used (LRU) Page Replacement
# Compilation
Command "make all" will properly compile and link the program files
into an executable which can then be invoked (see Usage).
# Usage:
Run Command: ./oss [-h] [-n proc] [-s simul]
              [-i intervalInMsToLaunchChildren] [-f logfile]
Examples: ./oss -h
            // will output the help/usage message
          ./oss -n 4 -s 2 -i 4 -f fileName
            // will run the program with the following arguments
            // 4 total children
            // 2 children able to be running simultaneously
            // 4 milliseconds between launching children
            // oss will write output to file "fileName" as well as console
# Outstanding Issues:
            - n/a
# Encountered Issues and Resolutions:
            - oss no longer launching processes after launching [-s] number,
              causing infinite loop of trying to launch processes but can't
                       - Resolution(s): (1) most output commented out temporarily for
                        debugging simplicity, adjusted placement of
                        activeChildren = countActiveChildren() -- failed
                        (2) worker termination logic and oss/worker message
                        passing verified to be working - workers not hitting
                        high enough access count to terminate before 5 IRL seconds
                        -- adjusted terminateThreshold to a lower amount to test
                        if processes can finish -- failed
                        (3) per instructor recommendation, removed usleep
                        --success!
