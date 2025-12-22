-- Initialize PostgreSQL database for TRX

-- Create the PERSON table based on the TRX record definition
CREATE TABLE IF NOT EXISTS person (
    id INTEGER PRIMARY KEY,
    name VARCHAR(64),
    age INTEGER,
    active BOOLEAN,
    salary DECIMAL(10,2)
);

-- Insert some sample data
INSERT INTO person (id, name, age, active, salary) VALUES
(1, 'John Doe', 30, true, 50000.00),
(2, 'Jane Smith', 25, false, 45000.00),
(3, 'Bob Johnson', 35, true, 60000.00);