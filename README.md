# File I/O using Byte-Range Locking
Experiment for evaluating the increase in the concurrency for file I/O  by multiple threads when byte-range locking is followed rather than file-level locking.

#Working Environment
This is a Win32 console application written on Microsoft Visual Studio 2010 suite, as a part of 2 month internship @GE Intelligent Platforms. Implements MSDN library methods and headers.

#Input
Sufficiently large input file for reading from/ writing to.

#Output
Output file containing properly formatted statistics of the result of the experiment.

