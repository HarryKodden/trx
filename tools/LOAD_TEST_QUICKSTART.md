# TRX Load Testing Quick Start

## What is this?

A comprehensive load testing tool that bombards your TRX REST API server with random, valid requests to test behavior under heavy concurrent load.

## Quick Start

### 1. Start the Server
```bash
make run
# Server will start on http://localhost:8080
```

### 2. Run Load Tests (in another terminal)

#### Predefined Tests
```bash
# Quick health check (100 requests, 10 users)
make load-test-quick

# Medium stress test (1000 requests, 20 users)
make load-test-medium

# Heavy load test (10000 requests, 50 users)
make load-test-heavy
```

#### Custom Tests
```bash
# 5000 requests with 30 concurrent users
make load-test ARGS="-n 5000 -c 30"

# Run for 2 minutes with 25 users
make load-test ARGS="-d 120 -c 25"

# Test against different URL
make load-test ARGS="-n 1000 -c 20 --url http://localhost:9000"
```

## What Does It Test?

The load tester randomly hits all API endpoints:

### PERSON CRUD
- GET /persons (get all)
- GET /person/{id}
- POST /persons (create)
- PUT /person/{id} (update)
- DELETE /person/{id}

### DEPARTMENT CRUD
- GET /departments
- GET /department/{id}
- POST /departments
- PUT /department/{id}
- DELETE /department/{id}

### EMPLOYEE CRUD
- GET /employees
- GET /employee/{person_id}
- POST /employees
- PUT /employee/{person_id}
- DELETE /employee/{person_id}

## Sample Output

```
TRX API Load Tester
Target: http://localhost:8080
Concurrency: 20 users
Total Requests: 1000

Progress: 1000/1000 requests, 245.3 req/s, 987/1000 successful

======================================================================
LOAD TEST RESULTS
======================================================================
Duration:          4.08s
Total Requests:    1000
Successful:        987 (98.7%)
Failed:            13 (1.3%)
Requests/sec:      245.30

Response Times:
  Min:             12.45ms
  Max:             456.78ms
  Mean:            45.23ms
  Median:          38.56ms
  P95:             89.12ms
  P99:             123.45ms
======================================================================
```

## Features

✅ **Realistic Data**: Generates valid random data (names, ages, departments, salaries)  
✅ **Smart ID Tracking**: Reuses created IDs for GET/PUT/DELETE operations  
✅ **Weighted Operations**: More GETs than POSTs/DELETEs (like real usage)  
✅ **Concurrent Users**: Simulates multiple users hitting API simultaneously  
✅ **Detailed Stats**: Response times, success rates, error analysis  
✅ **Connection Pooling**: Efficient HTTP connection management  

## Interpreting Results

### Success Rate
- **>95%**: ✅ Excellent - Server handles load well
- **90-95%**: ⚠️ Good - Minor issues under load
- **<90%**: ❌ Poor - Server struggling

### Response Times
- **P95 < 100ms**: ✅ Excellent responsiveness
- **P95 < 500ms**: ⚠️ Good for most use cases
- **P95 > 1000ms**: ❌ Slow - optimization needed

## Common Patterns

### Find Maximum Capacity
```bash
# Gradually increase concurrency until failure rate rises
make load-test ARGS="-d 60 -c 10"  # Start low
make load-test ARGS="-d 60 -c 25"  # Increase
make load-test ARGS="-d 60 -c 50"  # Keep going
make load-test ARGS="-d 60 -c 100" # Find limits
```

### Endurance Test (Memory Leaks)
```bash
# Run for 1 hour
make load-test ARGS="-d 3600 -c 20"
```

### Spike Test (Sudden Traffic)
```bash
# Burst of 10000 requests
make load-test ARGS="-n 10000 -c 100"
```

## Troubleshooting

### "Connection Error"
- Server not running? Run `make run` first
- Check server is on port 8080

### High Failure Rates
- Reduce concurrency: `-c 5`
- Increase timeout: `--timeout 10`
- Check server logs

### Need More Info?
See [tools/LOAD_TESTING.md](LOAD_TESTING.md) for full documentation.

## Direct Script Usage

If you prefer running the script directly:

```bash
# The wrapper handles virtual environment automatically
./tools/load_test.sh -n 1000 -c 20

# Or use the Python script directly
./tools/venv/bin/python tools/load_test.py -n 1000 -c 20
```

## Help

```bash
./tools/load_test.sh --help
```
