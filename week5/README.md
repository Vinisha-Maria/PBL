Week 5 - Page Replacement Simulator (C, pipes)
Page Replacement Simulator

A web-based Page Replacement Simulator developed in C to demonstrate and compare three operating system page replacement algorithms: FIFO, LRU, and Optimal.

The application allows users to enter a page reference string, select the number of memory frames, execute simulations, and view step-by-step results with performance statistics.

Features

- Simulates FIFO (First-In, First-Out) page replacement.
- Simulates LRU (Least Recently Used) page replacement.
- Simulates Optimal page replacement.
- Supports running one algorithm or comparing all three.
- Displays step-by-step frame allocation.
- Identifies page hits and page faults.
- Calculates hit ratio and fault ratio.
- Compares algorithms and identifies the algorithm with the fewest page faults.
- Displays a visual comparison chart.
- Validates user input.
- Uses separate processes for algorithm execution through IPC.
- Provides a browser-based interface.

Technologies Used

Backend

- C
- POSIX processes
- Pipes for inter-process communication
- Socket programming
- HTTP server

Frontend

- HTML5
- CSS3
- JavaScript
- Fetch API
- SVG charts

Build Tools

- GCC
- GNU Make

Project Structure

week5/
├── include/
│   └── common.h
├── src/
│   ├── algorithms.c
│   ├── ipc.c
│   ├── json.c
│   ├── server.c
│   ├── stats.c
│   └── validation.c
├── static/
│   ├── app.js
│   ├── index.html
│   └── style.css
├── Makefile
└── README.md

Requirements

- Linux operating system
- GCC compiler
- GNU Make
- A modern web browser

Installation and Setup

1. Clone the repository

git clone YOUR_REPOSITORY_URL

Replace "YOUR_REPOSITORY_URL" with your Git repository URL.

2. Enter the project directory

cd week5

3. Compile the project

make clean
make

This builds the "pagesim" executable.

4. Run the server

./pagesim

The server listens on port "5000" by default.

5. Open the application

Open your browser and visit:

http://localhost:5000

How to Use

1. Enter a page reference string using spaces or commas as separators.
2. Enter the number of frames (1–10).
3. Select FIFO, LRU, Optimal, or Compare All.
4. Click the Simulate button.
5. View the frame allocation table and simulation statistics.
6. When comparing algorithms, view the comparison table and page-fault chart.

Input Example

Reference String: 7 0 1 2 0 3 0 4 2 3
Number of Frames: 3
Algorithm: Compare All

Expected Sample Results

Algorithm| Page Hits| Page Faults
FIFO| 1| 9
LRU| 2| 8
Optimal| 4| 6

Best Algorithm: Optimal

Minimum Page Faults: 6

API

The frontend communicates with the C backend using an HTTP POST request.

Endpoint

POST /api/simulate

Request Example

{
  "reference": "7 0 1 2 0 3 0 4 2 3",
  "frames": 3,
  "algorithm": "all"
}

Supported algorithm values:

- "fifo"
- "lru"
- "optimal"
- "all"

The server returns JSON containing simulation results, page statistics, individual frame states, and algorithm comparisons.

Input Validation

The application enforces the following restrictions:

- Reference strings must contain between 1 and 100 page numbers.
- Page numbers must be whole numbers between 0 and 999.
- The number of frames must be between 1 and 10.
- Only FIFO, LRU, Optimal, or All are accepted as algorithm selections.

Cleaning Build Files

To remove compiled objects and the executable:

make clean

Learning Objectives

This project demonstrates:

- Operating system page replacement algorithms.
- Memory management concepts.
- Page hits and page faults.
- Inter-process communication using pipes.
- Process creation using "fork()".
- Client-server communication using sockets.
- JSON request and response handling.
- Frontend and backend integration.

Author

Add your name and academic details here.

License

This project is intended for educational and academic purposes.
