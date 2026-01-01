# Quick Start: Monitoring TRX Performance

Get up and running with performance monitoring in under 2 minutes.

## 🚀 Start Everything

```bash
# Start TRX server with PostgreSQL, Prometheus, and Grafana
docker compose up -d --build

# Wait ~30 seconds for all services to initialize
```

## 📊 Access the Dashboard

1. **Open Grafana**: http://localhost:3000
2. **Login**: 
   - Username: `admin`
   - Password: `admin`
3. **View Dashboard**: 
   - Navigate to **Dashboards** → **TRX Server Performance**
   - Dashboard auto-refreshes every 5 seconds

## 🧪 Generate Load & Watch

Open a new terminal and run a load test:

```bash
# Run heavy load test (10,000 requests with 50 concurrent users)
make load-test-heavy
```

Watch the Grafana dashboard while the test runs to see:
- **Request Rate** climbing to 100-500 req/s
- **Active Requests** fluctuating with concurrency
- **Response Time** showing average latency
- **Error Rate** tracking any failures

## 📈 What You'll See

The dashboard shows:

### Top Row - Current Stats
- **Total Requests**: 10,523
- **Active Requests**: 12 (concurrent)
- **Error Requests**: 87 (8.2% error rate)
- **Avg Response Time**: 45.2ms

### Graphs
1. **Request Rate** - Requests per second over time
2. **Response Time** - Average latency trend
3. **Error Rate** - Errors per second
4. **Active Requests** - Concurrency level over time

## 🔍 Other Interfaces

- **Prometheus**: http://localhost:9090
  - Run PromQL queries
  - Check scraping status at http://localhost:9090/targets

- **TRX Metrics (Raw)**: http://localhost:8080/metrics
  - Plain text Prometheus format
  - Useful for debugging

- **TRX Swagger UI**: http://localhost:8080/swagger
  - API documentation and testing

## 🛑 Stop Everything

```bash
docker compose down

# Or to remove volumes (clean slate)
docker compose down -v
```

## 💡 Tips

### Run Load Test While Watching Dashboard

```bash
# Terminal 1: Keep Grafana open in browser
open http://localhost:3000

# Terminal 2: Run load test
make load-test ARGS="-d 120 -c 30"  # 2 minutes, 30 concurrent users
```

### Quick Health Check

```bash
# Check all services are running
docker compose ps

# Check TRX metrics endpoint
curl http://localhost:8080/metrics

# Check Prometheus is scraping
curl http://localhost:9090/api/v1/targets | jq
```

### Custom Dashboard Queries

In Grafana, create your own panels with these PromQL queries:

```promql
# Success rate percentage
(1 - (rate(trx_error_requests[5m]) / rate(trx_total_requests[5m]))) * 100

# Requests per minute
rate(trx_total_requests[1m]) * 60

# Peak concurrency in last hour
max_over_time(trx_active_requests[1h])
```

## 📚 Next Steps

- Read [docs/MONITORING.md](../docs/MONITORING.md) for detailed documentation
- Explore Prometheus queries at http://localhost:9090/graph
- Customize the dashboard: Click panel → Edit → Modify query
- Set up alerts: Grafana → Alerting → Create alert rule

## 🐛 Troubleshooting

**Dashboard shows "No data"**
```bash
# Check if TRX server is up
curl http://localhost:8080/metrics

# Check Prometheus targets
open http://localhost:9090/targets

# Restart Grafana
docker compose restart grafana
```

**Can't access Grafana**
```bash
# Check if running
docker compose ps grafana

# Check logs
docker compose logs grafana

# Verify port not in use
lsof -i :3000
```

**Prometheus not scraping**
```bash
# Check Prometheus logs
docker compose logs prometheus | tail -20

# Verify TRX metrics endpoint
curl http://localhost:8080/metrics

# Check network connectivity
docker compose exec prometheus wget -O- http://trx:8080/metrics
```

That's it! You now have full monitoring for your TRX server. 🎉
