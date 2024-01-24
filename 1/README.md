## Disclaimer
__________
In this task, I'm using coroutines in C for handling file sorting, although practically, it doesn’t add much value in terms of performance. Coroutines are used within a single thread to manage multiple tasks, with the coro_yield function allowing each coroutine to pause and resume. This setup doesn't offer parallel processing benefits but serves as a method to organize and manage tasks in a sequential manner. The real advantage of coroutines in this context isn't performance but the structuring of code for handling multiple, sequential tasks efficiently.

