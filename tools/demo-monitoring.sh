#!/bin/bash
# Demo script: Load testing with live monitoring visualization

set -e

echo "╔════════════════════════════════════════════════════════════════╗"
echo "║  TRX Load Testing + Monitoring Demo                           ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if docker compose is running
if ! docker compose ps | grep -q "trx-trx-1.*Up"; then
    echo -e "${YELLOW}Starting Docker Compose stack...${NC}"
    docker compose up -d
    echo -e "${GREEN}✓ Services started${NC}"
    echo ""
    echo -e "${YELLOW}Waiting 10 seconds for services to initialize...${NC}"
    sleep 10
else
    echo -e "${GREEN}✓ Services already running${NC}"
fi

echo ""
echo -e "${BLUE}Access Points:${NC}"
echo "  • Grafana Dashboard: http://localhost:3000 (admin/admin)"
echo "  • Prometheus:        http://localhost:9090"
echo "  • TRX API:           http://localhost:8080"
echo "  • Swagger UI:        http://localhost:8080/swagger"
echo ""

# Open Grafana dashboard in browser (macOS)
if command -v open &> /dev/null; then
    echo -e "${YELLOW}Opening Grafana dashboard in browser...${NC}"
    open "http://localhost:3000/d/trx-performance/trx-server-performance"
    sleep 2
fi

echo ""
echo -e "${BLUE}Current Metrics:${NC}"
curl -s http://localhost:8080/metrics | grep -E "^trx_" | grep -v "^#"
echo ""

echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}Ready to run load test!${NC}"
echo ""
echo "Watch the Grafana dashboard while the test runs to see:"
echo "  • Request rate climbing"
echo "  • Active requests fluctuating"
echo "  • Response times under load"
echo "  • Error rate tracking"
echo ""
echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"
echo ""

# Ask user which test to run
echo "Select load test to run:"
echo "  1) Quick test  (100 requests, 10 concurrent users, ~5 seconds)"
echo "  2) Medium test (1000 requests, 20 concurrent users, ~30 seconds)"
echo "  3) Heavy test  (10000 requests, 50 concurrent users, ~2 minutes)"
echo "  4) Custom"
echo "  5) Skip test"
echo ""

read -p "Enter choice [1-5]: " choice

case $choice in
    1)
        echo -e "${GREEN}Running quick load test...${NC}"
        make load-test-quick
        ;;
    2)
        echo -e "${GREEN}Running medium load test...${NC}"
        make load-test-medium
        ;;
    3)
        echo -e "${GREEN}Running heavy load test...${NC}"
        make load-test-heavy
        ;;
    4)
        read -p "Enter number of requests: " num_requests
        read -p "Enter concurrency level: " concurrency
        echo -e "${GREEN}Running custom load test...${NC}"
        make load-test ARGS="-n $num_requests -c $concurrency"
        ;;
    5)
        echo -e "${YELLOW}Skipping load test${NC}"
        ;;
    *)
        echo -e "${YELLOW}Invalid choice, skipping load test${NC}"
        ;;
esac

echo ""
echo -e "${BLUE}Final Metrics:${NC}"
curl -s http://localhost:8080/metrics | grep -E "^trx_" | grep -v "^#"
echo ""

echo -e "${GREEN}═══════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}Demo complete!${NC}"
echo ""
echo "Next steps:"
echo "  • Explore Grafana dashboard: http://localhost:3000"
echo "  • Query Prometheus: http://localhost:9090/graph"
echo "  • Run another test: make load-test-heavy"
echo "  • Stop services: docker compose down"
echo ""
