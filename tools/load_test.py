#!/usr/bin/env python3
"""
TRX API Load Testing Script

Bombards the TRX server with random API requests to test behavior under heavy load.
Tests all CRUD operations across PERSON, DEPARTMENT, and EMPLOYEE resources.
"""

import argparse
import json
import random
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple
import requests
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry


@dataclass
class Stats:
    """Statistics for load test results"""
    total_requests: int = 0
    successful: int = 0
    failed: int = 0
    errors: Dict[str, int] = None
    response_times: List[float] = None
    
    def __post_init__(self):
        if self.errors is None:
            self.errors = {}
        if self.response_times is None:
            self.response_times = []


class TRXLoadTester:
    """Load tester for TRX REST API"""
    
    def __init__(self, base_url: str, timeout: int = 5):
        self.base_url = base_url.rstrip('/')
        self.timeout = timeout
        self.session = self._create_session()
        self.stats = Stats()
        
        # Track created IDs for realistic testing
        self.person_ids: List[int] = []
        self.department_ids: List[int] = []
        self.employee_person_ids: List[int] = []
        
        # Random data generators
        self.names = ["Alice", "Bob", "Charlie", "Diana", "Eve", "Frank", "Grace", "Henry", "Ivy", "Jack",
                     "Kate", "Liam", "Mia", "Noah", "Olivia", "Peter", "Quinn", "Ruby", "Sam", "Tina"]
        self.departments_names = ["Engineering", "Sales", "Marketing", "HR", "Finance", "Operations", 
                                 "Product", "Legal", "Support", "Research"]
    
    def _create_session(self) -> requests.Session:
        """Create a session with retry logic"""
        session = requests.Session()
        retry = Retry(
            total=3,
            backoff_factor=0.1,
            status_forcelist=[500, 502, 503, 504],
        )
        adapter = HTTPAdapter(max_retries=retry, pool_connections=100, pool_maxsize=100)
        session.mount('http://', adapter)
        session.mount('https://', adapter)
        return session
    
    def _make_request(self, method: str, endpoint: str, json_data: Optional[Dict] = None) -> Tuple[bool, float, Optional[str]]:
        """Make an HTTP request and return (success, response_time, error_msg)"""
        url = f"{self.base_url}{endpoint}"
        start_time = time.time()
        
        try:
            if method == 'GET':
                response = self.session.get(url, timeout=self.timeout)
            elif method == 'POST':
                response = self.session.post(url, json=json_data, timeout=self.timeout)
            elif method == 'PUT':
                response = self.session.put(url, json=json_data, timeout=self.timeout)
            elif method == 'DELETE':
                response = self.session.delete(url, timeout=self.timeout)
            else:
                return False, 0.0, f"Unknown method: {method}"
            
            elapsed = time.time() - start_time
            
            # Consider 2xx and 404 (not found) as success for testing purposes
            # 404 is expected when trying to GET/PUT/DELETE non-existent resources
            if response.status_code < 400 or response.status_code == 404:
                return True, elapsed, None
            else:
                return False, elapsed, f"HTTP {response.status_code}: {response.text[:100]}"
                
        except requests.exceptions.Timeout:
            elapsed = time.time() - start_time
            return False, elapsed, "Timeout"
        except requests.exceptions.ConnectionError:
            elapsed = time.time() - start_time
            return False, elapsed, "Connection Error"
        except Exception as e:
            elapsed = time.time() - start_time
            return False, elapsed, str(e)[:100]
    
    def _random_person_data(self) -> Dict:
        """Generate random PERSON data"""
        return {
            "name": random.choice(self.names),
            "age": random.randint(18, 65),
            "active": random.choice([True, False]),
            "salary": round(random.uniform(30000, 150000), 2)
        }
    
    def _random_department_data(self) -> Dict:
        """Generate random DEPARTMENT data"""
        return {
            "name": random.choice(self.departments_names),
            "budget": round(random.uniform(100000, 1000000), 2),
            "location": random.choice(["Building A", "Building B", "Building C", "Building D", "Remote"])
        }
    
    def _random_employee_data(self) -> Dict:
        """Generate random EMPLOYEE data"""
        # Use existing person_id or create a new one
        person_id = random.choice(self.person_ids) if self.person_ids and random.random() > 0.3 else random.randint(1, 10000)
        return {
            "person_id": person_id,
            "department": random.choice(self.departments_names)
        }
    
    def _random_id(self, id_pool: List[int], max_id: int = 10000) -> int:
        """Get a random ID from pool or generate a new one"""
        if id_pool and random.random() > 0.3:
            return random.choice(id_pool)
        return random.randint(1, max_id)
    
    def test_persons_get(self) -> Tuple[bool, float, Optional[str]]:
        """GET /persons"""
        success, elapsed, error = self._make_request('GET', '/persons')
        if success:
            # Try to extract person IDs from response for future use
            try:
                response = self.session.get(f"{self.base_url}/persons", timeout=self.timeout)
                if response.status_code == 200:
                    data = response.json()
                    if 'persons' in data:
                        ids = [p.get('id') for p in data['persons'] if 'id' in p]
                        self.person_ids.extend(ids)
                        self.person_ids = list(set(self.person_ids))[:100]  # Keep max 100 IDs
            except:
                pass
        return success, elapsed, error
    
    def test_person_get(self) -> Tuple[bool, float, Optional[str]]:
        """GET /person/{id}"""
        person_id = self._random_id(self.person_ids)
        return self._make_request('GET', f'/person/{person_id}')
    
    def test_persons_post(self) -> Tuple[bool, float, Optional[str]]:
        """POST /persons"""
        data = self._random_person_data()
        success, elapsed, error = self._make_request('POST', '/persons', data)
        if success:
            # Try to get the created ID
            try:
                response = self.session.post(f"{self.base_url}/persons", json=data, timeout=self.timeout)
                if response.status_code == 200:
                    result = response.json()
                    if 'id' in result:
                        self.person_ids.append(result['id'])
            except:
                pass
        return success, elapsed, error
    
    def test_person_put(self) -> Tuple[bool, float, Optional[str]]:
        """PUT /person/{id}"""
        person_id = self._random_id(self.person_ids)
        data = self._random_person_data()
        return self._make_request('PUT', f'/person/{person_id}', data)
    
    def test_person_delete(self) -> Tuple[bool, float, Optional[str]]:
        """DELETE /person/{id}"""
        person_id = self._random_id(self.person_ids)
        success, elapsed, error = self._make_request('DELETE', f'/person/{person_id}')
        if success and person_id in self.person_ids:
            self.person_ids.remove(person_id)
        return success, elapsed, error
    
    def test_departments_get(self) -> Tuple[bool, float, Optional[str]]:
        """GET /departments"""
        success, elapsed, error = self._make_request('GET', '/departments')
        if success:
            try:
                response = self.session.get(f"{self.base_url}/departments", timeout=self.timeout)
                if response.status_code == 200:
                    data = response.json()
                    if 'departments' in data:
                        ids = [d.get('id') for d in data['departments'] if 'id' in d]
                        self.department_ids.extend(ids)
                        self.department_ids = list(set(self.department_ids))[:100]
            except:
                pass
        return success, elapsed, error
    
    def test_department_get(self) -> Tuple[bool, float, Optional[str]]:
        """GET /department/{id}"""
        dept_id = self._random_id(self.department_ids)
        return self._make_request('GET', f'/department/{dept_id}')
    
    def test_departments_post(self) -> Tuple[bool, float, Optional[str]]:
        """POST /departments"""
        data = self._random_department_data()
        success, elapsed, error = self._make_request('POST', '/departments', data)
        if success:
            try:
                response = self.session.post(f"{self.base_url}/departments", json=data, timeout=self.timeout)
                if response.status_code == 200:
                    result = response.json()
                    if 'id' in result:
                        self.department_ids.append(result['id'])
            except:
                pass
        return success, elapsed, error
    
    def test_department_put(self) -> Tuple[bool, float, Optional[str]]:
        """PUT /department/{id}"""
        dept_id = self._random_id(self.department_ids)
        data = self._random_department_data()
        return self._make_request('PUT', f'/department/{dept_id}', data)
    
    def test_department_delete(self) -> Tuple[bool, float, Optional[str]]:
        """DELETE /department/{id}"""
        dept_id = self._random_id(self.department_ids)
        success, elapsed, error = self._make_request('DELETE', f'/department/{dept_id}')
        if success and dept_id in self.department_ids:
            self.department_ids.remove(dept_id)
        return success, elapsed, error
    
    def test_employees_get(self) -> Tuple[bool, float, Optional[str]]:
        """GET /employees"""
        success, elapsed, error = self._make_request('GET', '/employees')
        if success:
            try:
                response = self.session.get(f"{self.base_url}/employees", timeout=self.timeout)
                if response.status_code == 200:
                    data = response.json()
                    if 'employees' in data:
                        ids = [e.get('person_id') for e in data['employees'] if 'person_id' in e]
                        self.employee_person_ids.extend(ids)
                        self.employee_person_ids = list(set(self.employee_person_ids))[:100]
            except:
                pass
        return success, elapsed, error
    
    def test_employee_get(self) -> Tuple[bool, float, Optional[str]]:
        """GET /employee/{person_id}"""
        person_id = self._random_id(self.employee_person_ids, max_id=1000)
        return self._make_request('GET', f'/employee/{person_id}')
    
    def test_employees_post(self) -> Tuple[bool, float, Optional[str]]:
        """POST /employees"""
        data = self._random_employee_data()
        success, elapsed, error = self._make_request('POST', '/employees', data)
        if success:
            self.employee_person_ids.append(data['person_id'])
        return success, elapsed, error
    
    def test_employee_put(self) -> Tuple[bool, float, Optional[str]]:
        """PUT /employee/{person_id}"""
        person_id = self._random_id(self.employee_person_ids, max_id=1000)
        data = self._random_employee_data()
        return self._make_request('PUT', f'/employee/{person_id}', data)
    
    def test_employee_delete(self) -> Tuple[bool, float, Optional[str]]:
        """DELETE /employee/{person_id}"""
        person_id = self._random_id(self.employee_person_ids, max_id=1000)
        success, elapsed, error = self._make_request('DELETE', f'/employee/{person_id}')
        if success and person_id in self.employee_person_ids:
            self.employee_person_ids.remove(person_id)
        return success, elapsed, error
    
    def get_random_test(self):
        """Get a random test function"""
        tests = [
            # PERSON endpoints (weight: higher for GETs)
            (self.test_persons_get, 3),
            (self.test_person_get, 3),
            (self.test_persons_post, 2),
            (self.test_person_put, 2),
            (self.test_person_delete, 1),
            # DEPARTMENT endpoints
            (self.test_departments_get, 3),
            (self.test_department_get, 3),
            (self.test_departments_post, 2),
            (self.test_department_put, 2),
            (self.test_department_delete, 1),
            # EMPLOYEE endpoints
            (self.test_employees_get, 3),
            (self.test_employee_get, 3),
            (self.test_employees_post, 2),
            (self.test_employee_put, 2),
            (self.test_employee_delete, 1),
        ]
        
        # Weighted random selection
        test_func = random.choices([t[0] for t in tests], weights=[t[1] for t in tests])[0]
        return test_func
    
    def run_single_test(self) -> Tuple[bool, float, Optional[str]]:
        """Run a single random test"""
        test_func = self.get_random_test()
        return test_func()
    
    def record_result(self, success: bool, elapsed: float, error: Optional[str]):
        """Record test result in statistics"""
        self.stats.total_requests += 1
        self.stats.response_times.append(elapsed)
        
        if success:
            self.stats.successful += 1
        else:
            self.stats.failed += 1
            error_key = error if error else "Unknown"
            self.stats.errors[error_key] = self.stats.errors.get(error_key, 0) + 1
    
    def print_stats(self, duration: float):
        """Print test statistics"""
        print("\n" + "="*70)
        print("LOAD TEST RESULTS")
        print("="*70)
        print(f"Duration:          {duration:.2f}s")
        print(f"Total Requests:    {self.stats.total_requests}")
        print(f"Successful:        {self.stats.successful} ({100*self.stats.successful/max(1,self.stats.total_requests):.1f}%)")
        print(f"Failed:            {self.stats.failed} ({100*self.stats.failed/max(1,self.stats.total_requests):.1f}%)")
        print(f"Requests/sec:      {self.stats.total_requests/max(0.001,duration):.2f}")
        
        if self.stats.response_times:
            sorted_times = sorted(self.stats.response_times)
            print(f"\nResponse Times:")
            print(f"  Min:             {min(sorted_times)*1000:.2f}ms")
            print(f"  Max:             {max(sorted_times)*1000:.2f}ms")
            print(f"  Mean:            {sum(sorted_times)/len(sorted_times)*1000:.2f}ms")
            print(f"  Median:          {sorted_times[len(sorted_times)//2]*1000:.2f}ms")
            print(f"  P95:             {sorted_times[int(len(sorted_times)*0.95)]*1000:.2f}ms")
            print(f"  P99:             {sorted_times[int(len(sorted_times)*0.99)]*1000:.2f}ms")
        
        if self.stats.errors:
            print(f"\nErrors:")
            for error, count in sorted(self.stats.errors.items(), key=lambda x: x[1], reverse=True):
                print(f"  {error}: {count}")
        
        print("="*70)


def main():
    parser = argparse.ArgumentParser(
        description='TRX API Load Tester - Bombard the server with random requests',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Quick test with 100 requests, 10 concurrent users
  %(prog)s -n 100 -c 10

  # Heavy load test with 10000 requests, 50 concurrent users
  %(prog)s -n 10000 -c 50

  # Continuous load for 60 seconds with 20 concurrent users
  %(prog)s -d 60 -c 20

  # Test against custom URL
  %(prog)s -n 1000 -c 20 --url http://localhost:9000
        """
    )
    
    parser.add_argument('-n', '--num-requests', type=int, default=1000,
                       help='Total number of requests to send (default: 1000)')
    parser.add_argument('-c', '--concurrency', type=int, default=10,
                       help='Number of concurrent users/threads (default: 10)')
    parser.add_argument('-d', '--duration', type=int,
                       help='Run for specified duration in seconds (overrides -n)')
    parser.add_argument('--url', default='http://localhost:8080',
                       help='Base URL of TRX server (default: http://localhost:8080)')
    parser.add_argument('--timeout', type=int, default=5,
                       help='Request timeout in seconds (default: 5)')
    parser.add_argument('--warmup', type=int, default=10,
                       help='Number of warmup requests before main test (default: 10)')
    
    args = parser.parse_args()
    
    print(f"TRX API Load Tester")
    print(f"Target: {args.url}")
    print(f"Concurrency: {args.concurrency} users")
    
    if args.duration:
        print(f"Duration: {args.duration}s")
    else:
        print(f"Total Requests: {args.num_requests}")
    
    print(f"\nWarming up with {args.warmup} requests...")
    
    tester = TRXLoadTester(args.url, timeout=args.timeout)
    
    # Warmup phase
    for _ in range(args.warmup):
        try:
            tester.run_single_test()
        except:
            pass
    
    # Reset stats after warmup
    tester.stats = Stats()
    
    print("Starting load test...\n")
    start_time = time.time()
    
    try:
        if args.duration:
            # Duration-based testing
            end_time = start_time + args.duration
            request_count = 0
            
            with ThreadPoolExecutor(max_workers=args.concurrency) as executor:
                futures = []
                
                while time.time() < end_time:
                    # Submit requests in batches
                    batch_size = min(args.concurrency * 2, 100)
                    for _ in range(batch_size):
                        if time.time() >= end_time:
                            break
                        future = executor.submit(tester.run_single_test)
                        futures.append(future)
                        request_count += 1
                    
                    # Process completed futures
                    for future in as_completed(futures[:args.concurrency]):
                        success, elapsed, error = future.result()
                        tester.record_result(success, elapsed, error)
                        futures.remove(future)
                        
                        # Progress indicator
                        if tester.stats.total_requests % 100 == 0:
                            elapsed_time = time.time() - start_time
                            rps = tester.stats.total_requests / elapsed_time
                            print(f"Progress: {tester.stats.total_requests} requests, "
                                 f"{rps:.1f} req/s, "
                                 f"{tester.stats.successful}/{tester.stats.total_requests} successful", end='\r')
                
                # Wait for remaining futures
                for future in as_completed(futures):
                    success, elapsed, error = future.result()
                    tester.record_result(success, elapsed, error)
        
        else:
            # Count-based testing
            with ThreadPoolExecutor(max_workers=args.concurrency) as executor:
                futures = [executor.submit(tester.run_single_test) for _ in range(args.num_requests)]
                
                for i, future in enumerate(as_completed(futures)):
                    success, elapsed, error = future.result()
                    tester.record_result(success, elapsed, error)
                    
                    # Progress indicator every 100 requests
                    if (i + 1) % 100 == 0:
                        elapsed_time = time.time() - start_time
                        rps = (i + 1) / elapsed_time
                        print(f"Progress: {i+1}/{args.num_requests} requests, "
                             f"{rps:.1f} req/s, "
                             f"{tester.stats.successful}/{i+1} successful", end='\r')
    
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user!")
    
    duration = time.time() - start_time
    tester.print_stats(duration)


if __name__ == '__main__':
    main()
