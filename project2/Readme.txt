name: Lei Wen		ID #: 108278455
name: Xiaoyu Xu		ID #: 108397033		



Section 22.5
In the unprt.h header file set:
		RTT_RXTMIN to 1000
		RTT_RXTMAX to 3000
		RTT_MAXNREXMT to 12
For all variable: rtt_rtt, rtt_srtt, rtt_rttvar, rtt_rto in "unprtt.h", using datatype int instead of float.
For function  static float rtt_minmax(float), make if as 
		static int rtt_minmax(int rto)
NOTICE: all line described below are form textbook.
In rtt_init(struct rtt_info *ptr), make tv.tv_sec as:
line 22		tv.tv_sec * 1000000 + tv.tv_usec;
line 25		ptr->rtt_rttvar = 750000
In rtt_ts(struct rtt_info *ptr) function, make its as:
line 40		ts = (tv.tv_sec  * 1000000 - ptr->rtt_base ) + (tv.tv_usec) 
In rtt_start(struct rtt_info *ptr) function, make it as:
line 51		return ((int) (ptr->rtt_rto + 0.5));
In rtt_stop(struct rtt_info *ptr, uint32_t ms) function
ine 65		using int instead of double for the delta's datatype
line 66		ptr->rtf = ms
line 74		if (delta < 0)
In rtt_timeout(struct rtt_info *ptr) function
		add one more line between 86 and 87 as:
		ptr->rtt_rto = rtt_minmax(RTT_RTOCAL(ptr));
In client.c for alarm() function, it's input argument is based on second, so make it as:
		alarm(rtt_start(&rttinfo)/1000000);

		
Automatic Repeat-reQuest(ARQ) mechanism. 
For the server, 1th time cwnd size is 1, after one round trip and if the connect condition is OK, the cwnd size will become 2. And then next time 4, 8, 16… and so on.
When cwnd >ssthresh, after every round trip, cwnd = cwnd +1
If the got duplicate ACK three time, set ssthresh to one-half the current congestion window.
Each time another duplicate ACK arrives, increment cwnd by the segment size and transmit a packet(only if allowed by the new value of cwnd).
When the next ACK arrives that acknowledges new data, set cwnd to ssthresh.


We store the node in a link list as a buffer, at both server and client.
At server: after sending one packet, it stores all information in a node(struct datatype). And when receiving one ACK for the packet, the server should update the link list. Also keep the size of the link list smaller than window size.
At client, after receiving the packet, it put it in the link list. Only when another thread which is the print function print all the link list. The client then clear the link list. Also keep the size of the link list smaller than the window size.
















