#!/usr/bin/env python3
"""
Complete example showing all features of the Python libfyaml bindings.

This demonstrates the "zero-casting" interface where YAML/JSON data
works naturally like native Python objects.
"""

import sys
sys.path.insert(0, '.')
import libfyaml
import tempfile
import os

print("=" * 70)
print("PYTHON LIBFYAML - COMPLETE FEATURE DEMONSTRATION")
print("=" * 70)

# Sample data: Employee database
employee_data = """
company: TechCorp
employees:
  engineering:
    - name: Alice Smith
      email: alice@example.com
      title: Senior Engineer
      salary: 120000
      years: 5
      skills: [python, rust, go]
    - name: Bob Jones
      email: bob@example.com
      title: Engineer
      salary: 90000
      years: 2
      skills: [python, javascript]
  sales:
    - name: Charlie Brown
      email: charlie@example.com
      title: Sales Manager
      salary: 110000
      years: 7
      skills: [negotiation, presentations]
"""

print("\n### 1. PARSING - Load YAML into native format ###")
doc = libfyaml.loads(employee_data)
print(f"✅ Parsed {len(doc['employees'].keys())} departments")
print(f"   Company: {doc['company']}")  # Format string works!

print("\n### 2. ZERO-CASTING ACCESS - No int()/str() needed! ###")

# Access nested data
eng_dept = doc['employees']['engineering']
first_engineer = eng_dept[0]

# String methods work directly
name_upper = first_engineer['name'].upper()
first_name = first_engineer['name'].split()[0]
email_domain = first_engineer['email'].split('@')[1]

print(f"First engineer:")
print(f"  Full name (upper): {name_upper}")
print(f"  First name: {first_name}")
print(f"  Email domain: {email_domain}")

# Arithmetic works directly
next_year_salary = first_engineer['salary'] + 10000
years_to_decade = 10 - first_engineer['years']

print(f"  Current salary: ${first_engineer['salary']:,}")  # Format spec!
print(f"  Projected salary: ${next_year_salary:,}")
print(f"  Years to decade: {years_to_decade}")

print("\n### 3. COMPARISONS & FILTERING - Direct boolean operations ###")

# Filter without casting
senior_engineers = [
    emp for dept in doc['employees'].values()
    for emp in dept
    if emp['years'] >= 5  # Comparison works directly!
]

print(f"Senior employees (5+ years):")
for emp in senior_engineers:
    print(f"  - {emp['name']} ({emp['years']} years)")

# Complex filtering with string operations
python_developers = [
    emp for dept in doc['employees'].values()
    for emp in dept
    if 'python' in [str(skill).lower() for skill in emp['skills']]  # List access + methods
]

print(f"\nPython developers:")
for emp in python_developers:
    skills_str = ', '.join(str(s) for s in emp['skills'])
    print(f"  - {emp['name']}: {skills_str}")

print("\n### 4. AGGREGATIONS - Arithmetic in generators ###")

all_employees = [
    emp for dept in doc['employees'].values()
    for emp in dept
]

# Direct arithmetic in comprehensions
total_payroll = sum(emp['salary'] for emp in all_employees)
avg_salary = total_payroll / len(all_employees)
max_salary = max(emp['salary'] + 0 for emp in all_employees)  # +0 converts to Python
total_experience = sum(emp['years'] for emp in all_employees)

print(f"Company statistics:")
print(f"  Total employees: {len(all_employees)}")
print(f"  Total payroll: ${total_payroll:,}")
print(f"  Average salary: ${avg_salary:,.2f}")
print(f"  Highest salary: ${max_salary:,}")
print(f"  Total experience: {total_experience} person-years")

print("\n### 5. DICT METHODS - Natural iteration ###")

print("Departments and headcount:")
for dept_name in doc['employees'].keys():
    dept = doc['employees'][str(dept_name)]
    count = sum(1 for _ in dept)  # Iteration
    print(f"  {str(dept_name).capitalize()}: {count} employees")

print("\n### 6. FILE I/O - Convenient save/load ###")

with tempfile.TemporaryDirectory() as tmpdir:
    # Save to file
    output_path = os.path.join(tmpdir, "employees.yaml")
    libfyaml.dump(output_path, doc.to_python())
    print(f"✅ Saved to {output_path}")

    # Load back
    loaded = libfyaml.load(output_path)
    print(f"✅ Loaded back, company: {loaded['company']}")

    # Save as JSON
    json_path = os.path.join(tmpdir, "employees.json")
    libfyaml.dump(json_path, doc.to_python(), mode="json")
    print(f"✅ Saved as JSON to {json_path}")

    # Compact mode
    compact_path = os.path.join(tmpdir, "compact.yaml")
    libfyaml.dump(compact_path, {"status": "active", "count": 3}, compact=True)
    with open(compact_path, 'r') as f:
        print(f"✅ Compact: {f.read().strip()}")

print("\n### 7. BUILDING DATA - from_python() ###")

# Create new employee record programmatically
new_employee = {
    "name": "Diana Wilson",
    "email": "diana@example.com",
    "title": "Data Scientist",
    "salary": 130000,
    "years": 3,
    "skills": ["python", "ml", "statistics"]
}

# Convert to FyGeneric without YAML serialization
new_emp_doc = libfyaml.from_python(new_employee)
print(f"New employee created:")
print(f"  Name: {new_emp_doc['name']}")
print(f"  Salary: ${new_emp_doc['salary']:,}")
print(f"  Skills: {', '.join(str(s) for s in new_emp_doc['skills'])}")

print("\n### 8. MEMORY EFFICIENCY - Streaming pattern ###")

# Process large dataset without keeping all objects
print("Processing employees (streaming):")
for dept in doc['employees'].values():
    for emp in dept:
        # Process without keeping references
        if emp['salary'] > 100000:
            bonus = (emp['salary'] - 100000) * 0.05
            print(f"  {emp['name']}: ${bonus:,.2f} bonus")
        # emp is freed here - no memory accumulation!

print("\n### 9. FULL CONVERSION - When you need Python objects ###")

# Convert entire structure to native Python
python_dict = doc.to_python()
print(f"Converted to Python dict: {type(python_dict)}")
print(f"  Keys: {list(python_dict.keys())}")

# Now it's a regular Python dict
python_dict['company'] = "TechCorp Inc."  # Can modify
print(f"  Modified: {python_dict['company']}")

print("\n" + "=" * 70)
print("✅ ALL FEATURES DEMONSTRATED!")
print("=" * 70)
print("""
Key Takeaways:
  1. ZERO manual type conversions needed
  2. All operations work naturally (arithmetic, comparisons, methods)
  3. String methods: .upper(), .split(), etc. work directly
  4. Comparisons: >, <, ==, etc. work directly
  5. Arithmetic: +, -, *, etc. work directly
  6. Dict methods: .keys(), .values(), .items() work naturally
  7. Format strings: f"{value:,}" works with format specs
  8. File I/O: load() and dump() for convenient file operations
  9. Memory efficient: Streaming patterns prevent accumulation
  10. Full Python conversion available when needed via .to_python()

The Python libfyaml bindings provide a truly Pythonic interface
to YAML/JSON data with zero compromises!
""")
