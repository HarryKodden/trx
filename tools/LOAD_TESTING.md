# TRX Load Testing Guide

## Overview

The `load_test.py` script bombards the TRX server with random API requests to test behavior under heavy load. It tests all CRUD operations across PERSON, DEPARTMENT, and EMPLOYEE resources with realistic data patterns.

## Features

- **Random Request Generation**: Intelligently generates valid data for all API endpoints
- **Concurrent Users**: Simulates multiple users hitting the API simultaneously
- **Weighted Operations**: GET operations are more frequent than POST/PUT/DELETE (realistic usage)
- **Smart ID Tracking**: Tracks created resource IDs and reuses them for realistic testing
- **Connection Pooling**: Efficient HTTP connection management for high throughput
- **Detailed Statistics**: Response times (min/max/mean/median/P95/P99), success rates, error analysis

## Installation

### Option 1: Using Virtual Environment (Recommended)

```bash
cd /Users/kodde001/Projects/trx/tools
python3 -m venv venv
source venv/bin/activate
pip install requests
```

Then run the script with:
```bash
./venv/bin/python load_test.py -n 1000 -c 20
```

### Option 2: Using Homebrew

```bash
brew install python-requests
```

### Option 3: Break System Packages (Not Recommended)

```bash
python3 -m pip install --break-system-packages requests
```

## Usage

### Quick Tests

```bash
# Simple test: 100 requests, 10 concurrent users
./tools/load_test.py -n 100 -c 10

# Medium load: 1000 requests, 20 concurrent users
./tools/load_test.py -n 1000 -c 20

# Heavy load: 10000 requests, 50 concurrent users
./tools/load_test.py -n 10000 -c 50
```

### Duration-Based Testing

```bash
# Run for 60 seconds with 20 concurrent users
./tools/load_test.py -d 60 -c 20

# Run for 5 minutes with 30 concurrent users
./tools/load_test.py -d 300 -c 30
```

### Custom Configuration

```bash
# Custom server URL
./tools/load_test.py -n 1000 -c 20 --url http://localhost:9000

# Adjust timeout for slower responses
./tools/load_test.py -n 1000 -c 10 --timeout 10

# Longer warmup period
./tools/load_test.py -n 5000 -c 25 --warmup 50
```

## Command-Line Options

```
-n, --num-requests   Total number of requests (default: 1000)
-c, --concurrency    Number of concurrent users (default: 10)
-d, --duration       Run for specified seconds (overrides -n)
--url                Base URL of server (default: http://localhost:8080)
--timeout            Request timeout in seconds (default: 5)
--warmup             Warmup requests before test (default: 10)
```

## Example Output

```
TRX API Load Tester
Target: http://localhost:8080
Concurrency: 20 users
Total Requests: 1000

Warming up with 10 requests...
Starting load test...

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

Errors:
  HTTP 404: Not Found: 8
  Timeout: 3
  Connection Error: 2
======================================================================
```

## Test Strategy

The load tester uses a realistic mix of operations:

### Operation Weights
- **GET operations** (3x weight): Most common in real APIs
  - GET /persons, GET /departments, GET /employees
  - GET /person/{id}, GET /department/{id}, GET /employee/{person_id}

- **POST/PUT operations** (2x weight): Moderate frequency
  - POST /persons, POST /departments, POST /employees
  - PUT /person/{id}, PUT /department/{id}, PUT /employee/{person_id}

- **DELETE operations** (1x weight): Least common
  - DELETE /person/{id}, DELETE /department/{id}, DELETE /employee/{person_id}

### Data Generation
- **Person**: Random names, ages (18-65), active status, salaries ($30k-$150k)
- **Department**: Random department names from realistic pool
- **Employee**: Associates persons with departments

### ID Management
- Tracks created IDs during testing
- Reuses IDs for GET/PUT/DELETE operations (70% of the time)
- Falls back to random IDs (30% of the time) to test error handling
- Maintains pools of up to 100 IDs per resource type

## Typical Scenarios

### 1. Basic Health Check
```bash
# Quick verification that server handles concurrent requests
./tools/load_test.py -n 100 -c 5
```

### 2. Capacity Testing
```bash
# Find maximum throughput
./tools/load_test.py -d 120 -c 50
```

### 3. Endurance Testing
```bash
# Long-running test to detect memory leaks
./tools/load_test.py -d 3600 -c 20
```

### 4. Spike Testing
```bash
# Sudden burst of traffic
./tools/load_test.py -n 10000 -c 100
```

## Integration with Development

### Before Deploying
```bash
# Start server
make run

# In another terminal, run load test
./tools/load_test.py -n 5000 -c 30

# Check for memory leaks or performance degradation
```

### CI/CD Integration
```bash
# In CI pipeline
docker compose up -d --build
sleep 5  # Wait for server startup
./tools/load_test.py -n 1000 -c 20 --timeout 10
EXIT_CODE=$?
docker compose down
exit $EXIT_CODE
```

## Interpreting Results

### Success Rate
- **>95%**: Excellent - Server handles load well
- **90-95%**: Good - Minor issues under load
- **80-90%**: Fair - Performance degradation evident
- **<80%**: Poor - Server struggling, needs optimization

### Response Times
- **P95 < 100ms**: Excellent responsiveness
- **P95 < 500ms**: Good for most use cases
- **P95 < 1000ms**: Acceptable for batch operations
- **P95 > 1000ms**: Slow - optimization needed

### Requests/Second
- Depends on hardware and operations
- Compare baseline vs. load to detect degradation
- Monitor for consistent throughput over time

## Troubleshooting

### Connection Errors
- Server not running: `make run`
- Wrong URL: Use `--url` to specify correct endpoint
- Firewall/network issues: Check connectivity

### High Failure Rates
- Increase timeout: `--timeout 10`
- Reduce concurrency: `-c 5`
- Check server logs for errors

### Low Throughput
- Increase concurrency: `-c 50`
- Check server CPU/memory usage
- Profile server code for bottlenecks

## Best Practices

1. **Start Small**: Begin with low concurrency and increase gradually
2. **Warmup**: Let the script do warmup to populate ID pools
3. **Monitor Server**: Watch CPU, memory, and logs during testing
4. **Baseline**: Establish baseline performance before changes
5. **Consistent Environment**: Use same hardware/config for comparisons
6. **Multiple Runs**: Run tests multiple times for statistical validity
