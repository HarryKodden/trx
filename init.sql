-- Initialize PostgreSQL database for TRX

-- Create the DEPARTMENT table
CREATE TABLE IF NOT EXISTS department (
    id INTEGER PRIMARY KEY,
    name VARCHAR(64) NOT NULL,
    budget DECIMAL(12,2),
    location VARCHAR(128)
);

INSERT INTO department (id, name, budget, location) VALUES
(1, 'Engineering', 500000.00, 'Building A'),
(2, 'Sales', 300000.00, 'Building B'),
(3, 'Marketing', 200000.00, 'Building C');

-- Create the PERSON table based on the TRX record definition
CREATE TABLE IF NOT EXISTS person (
    id INTEGER PRIMARY KEY,
    name VARCHAR(64),
    age INTEGER,
    active BOOLEAN,
    salary DECIMAL(10,2)
);

INSERT INTO person (id, name, age, active, salary) VALUES
(1, 'Alice', 30, true, 70000.00),
(2, 'Bob', 25, true, 50000.00),
(3, 'Charlie', 35, false, 60000.00);

-- Create the EMPLOYEE table
CREATE TABLE IF NOT EXISTS employee (
    person_id INTEGER PRIMARY KEY REFERENCES person(id),
    department VARCHAR(32)
);

-- Insert some sample employee data
INSERT INTO employee (person_id, department) VALUES
(1, 'Engineering'),
(2, 'Marketing'),
(3, 'Sales');