package parhash

import (	
	"context"
	"net"
	"sync"
	"log"
	"time"

	"github.com/pkg/errors"
	"golang.org/x/sync/semaphore"
	"google.golang.org/grpc"

	hashpb "fs101ex/pkg/gen/hashsvc"
	parhashpb "fs101ex/pkg/gen/parhashsvc"

	"fs101ex/pkg/workgroup"

	"github.com/prometheus/client_golang/prometheus"
)

type Config struct {
	ListenAddr   string
	BackendAddrs []string
	Concurrency  int

	Prom prometheus.Registerer
}

// Implement a server that responds to ParallelHash()
// as declared in /proto/parhash.proto.
//
// The implementation of ParallelHash() must not hash the content
// of buffers on its own. Instead, it must send buffers to backends
// to compute hashes. Buffers must be fanned out to backends in the
// round-robin fashion.
//
// For example, suppose that 2 backends are configured and ParallelHash()
// is called to compute hashes of 5 buffers. In this case it may assign
// buffers to backends in this way:
//
//	backend 0: buffers 0, 2, and 4,
//	backend 1: buffers 1 and 3.
//
// Requests to hash individual buffers must be issued concurrently.
// Goroutines that issue them must run within /pkg/workgroup/Wg. The
// concurrency within workgroups must be limited by Server.sem.
//
// WARNING: requests to ParallelHash() may be concurrent, too.
// Make sure that the round-robin fanout works in that case too,
// and evenly distributes the load across backends.
//
// The server must report the following performance counters to Prometheus:
//
//  1. nr_nr_requests: a counter that is incremented every time a call
//     is made to ParallelHash(),
//
//  2. subquery_durations: a histogram that tracks durations of calls
//     to backends.
//     It must have a label `backend`.
//     Each subquery_durations{backed=backend_addr} must be a histogram
//     with 24 exponentially growing buckets ranging from 0.1ms to 10s.
//
// Both performance counters must be placed to Prometheus namespace "parhash".
type Metrics struct {
	//counter and histogram
	nr_nr_requests		prometheus.Counter
	subquery_durations	*prometheus.HistogramVec
}

type Server struct {
	conf Config

	sem *semaphore.Weighted

	//from server.go
	stop context.CancelFunc
	l    net.Listener
	wg   sync.WaitGroup

	//extra
	lock  sync.Mutex
	last_backend int

	//metrics
	metrics	*Metrics
}

func NewMetrics(reg prometheus.Registerer) *Metrics {
  m := &Metrics{
	nr_nr_requests: prometheus.NewCounter(prometheus.CounterOpts{
		Namespace: "parhash",
		Name: "nr_nr_requests",
		Help: "a counter that is incremented every time a call is made to ParallelHash()",
	}),
    subquery_durations: prometheus.NewHistogramVec(prometheus.HistogramOpts{
		Namespace: "parhash",
		Name: "subquery_durations",
		Buckets: prometheus.ExponentialBuckets(100, 10000000, 24),
		Help: "a histogram that tracks durations of calls to backends",
	},
	//label?
	[]string{"backend"}),
  }
  return m
}

func New(conf Config) *Server {
	return &Server{
		conf: conf,
		sem:  semaphore.NewWeighted(int64(conf.Concurrency)),
		metrics: NewMetrics(conf.Prom),
	}
}

func (s *Server) Start(ctx context.Context) (err error) {
	defer func() { err = errors.Wrap(err, "Start()") }()

	ctx, s.stop = context.WithCancel(ctx)

	s.l, err = net.Listen("tcp", s.conf.ListenAddr)
	if err != nil {
		return err
	}

	srv := grpc.NewServer()
	s.conf.Prom.MustRegister(s.metrics.nr_nr_requests)
	s.conf.Prom.MustRegister(s.metrics.subquery_durations)
	parhashpb.RegisterParallelHashSvcServer(srv, s)

	s.wg.Add(2)
	go func() {
		defer s.wg.Done()

		srv.Serve(s.l)
	}()
	go func() {
		defer s.wg.Done()

		<-ctx.Done()
		s.l.Close()
	}()

	return nil
}

func (s *Server) ListenAddr() string {
	return s.l.Addr().String()
}

func (s *Server) Stop() {
	s.stop()
	s.wg.Wait()
}

func (s *Server) ParallelHash(ctx context.Context, req *parhashpb.ParHashReq) (resp *parhashpb.ParHashResp, err error) {
	s.metrics.nr_nr_requests.Inc()
	var (
		total_backends = len(s.conf.BackendAddrs)
		clients = make([]hashpb.HashSvcClient, total_backends)
		connections = make([]*grpc.ClientConn, total_backends)
	)

	for i := range clients {
		//from sum.go
		connections[i], err = grpc.Dial(s.conf.BackendAddrs[i],
			grpc.WithInsecure(), /* allow non-TLS connections */
		)
		if err != nil {
			log.Fatalf("failed to connect to %q: %v", s.conf.BackendAddrs[i], err)
		}
		defer connections[i].Close()
	
		clients[i] = hashpb.NewHashSvcClient(connections[i])
	}
	//from sum.go
	var (
		wg     = workgroup.New(workgroup.Config{Sem: s.sem})
		hashes = make([][]byte, len(req.Data))
		lock   sync.Mutex
	)
	for i := range req.Data {
		hash_num := i

		wg.Go(ctx, func(ctx context.Context) (err error) {
			s.lock.Lock()
			num_backend := s.last_backend
			s.last_backend = (s.last_backend + 1) % total_backends
			s.lock.Unlock()

			start := time.Now()
			resp, err := clients[num_backend].Hash(ctx, &hashpb.HashReq{Data: req.Data[hash_num]})
			finish := time.Since(start)
			s.metrics.subquery_durations.With(prometheus.Labels{"backend": s.conf.BackendAddrs[num_backend]}).Observe(float64(finish.Microseconds()))
			if err != nil {
				return err
			}

			lock.Lock()
			hashes[hash_num] = resp.Hash
			lock.Unlock()

			return nil
		})
	}
	if err := wg.Wait(); err != nil {
		log.Fatalf("failed to hash files: %v", err)
	}
	//h := sha256.Sum256(req.Data)
	return &parhashpb.ParHashResp{Hashes: hashes}, nil
}