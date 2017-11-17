

***tsan/rtl/ufo/doc/***

***questions -> email:feedback2bowen@outlook.com***





_LOG:_
================== 2017-9-19 ==================
1. developed the new allocation mechanism:
modified TSAN code so that application memory deallocations are deferred and a batch deallocation is performed at some trigger event (total number of memory exceeded the threshold).

2. developed the new basic interfaces for trace event handling:
removed the file I/O related code and add an interface to process the events in a global view. Now the runtime will trace application as usual, but when the total number of events exceeded the threshold, all thread local events are dropped.

In a typical execution, Chromium ran for nearly 2 mins, 4 webpages (simple page, pages like gmail and youtube crashes my Chromium) were visited. In the background, 5 processes were created, and each performed some batch memory release (72 in total):
```
>>>batch_dealloc proc(28675 0)#36  size:22968  num:230  total 47
>>>batch_dealloc proc(28678 0)#0  size:7232766  num:4453  total 1
>>>batch_dealloc proc(28737 1)#0  size:39692365  num:135684  total 11
>>>batch_dealloc proc(28831 1)#0  size:42949755  num:85569  total 3
>>>batch_dealloc proc(28820 1)#0  size:14295870  num:73451  total 10
```


================== 2017-9-30 ==================
1. developed the report functions, now it will report precise call stack as TSAN does
2. integrated the report, new allocator and event buffer together
3. created a demo function and tested it on sample c++ code.


In folder "test_c", there is a sample cpp code, run it with poj179 yields:
![Analyze output](https://github.tamu.edu/raw/bowen-cai/POJ179/master/__test_code/Screenshot%20from%202017-10-01%2019%3A03%3A16.png?token=AAAK3Vt6FC79enlEz3UlJXgzEWH2vGx4ks5Z2r41wA%3D%3D)

the complete output is in "alloc2_output.txt",

Each heap memory access is preciese matched to the alloc event, regardless of any possible overlapping allocations.
However there are some issues with multi-threaded programs, as the memory access matching in "test2()" is not successful.

================== 2017-10-10 ==================
* fixed bugs, tested on Chrome

Sample output from Chrome:
https://github.tamu.edu/bowen-cai/POJ179/blob/master/__test_code/sample_output_chrome.txt

What's next:
* Discuss with Gang and add more API for future analysis
* make the tool more configurable (change hard coded parameters)

