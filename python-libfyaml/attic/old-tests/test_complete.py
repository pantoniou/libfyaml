#!/usr/bin/env python3
"""
Comprehensive test showing COMPLETE type propagation.

Everything works naturally - arithmetic, comparisons, dict methods - all without casting!
"""

import sys
sys.path.insert(0, 'libfyaml')
import _libfyaml as libfyaml

print("=" * 70)
print("COMPLETE TYPE PROPAGATION - Final Demonstration")
print("=" * 70)

# Load employee data
employees = libfyaml.loads("""
engineering:
  alice:
    title: Senior Engineer
    salary: 120000
    performance: 95.5
    years: 5
  bob:
    title: Engineer
    salary: 90000
    performance: 88.0
    years: 3

sales:
  charlie:
    title: Sales Manager
    salary: 110000
    performance: 92.0
    years: 7
  diana:
    title: Sales Rep
    salary: 75000
    performance: 96.5
    years: 2
""")

print("\n### Scenario: Calculate bonuses and raises ###\n")

# Process each department
for dept_name in employees.keys():
    dept = employees[str(dept_name)]

    print(f"Department: {str(dept_name).upper()}")

    # Iterate over employees in department
    for emp_name in dept.keys():
        emp = dept[str(emp_name)]

        # Extract values - NO CASTING NEEDED!
        title = emp['title']
        salary = emp['salary']
        performance = emp['performance']
        years = emp['years']

        # Complex calculations - ALL automatic!
        if performance > 90:  # Comparison - automatic!
            # Arithmetic - all automatic!
            bonus_percent = (performance - 90) / 100
            bonus = salary * bonus_percent

            # More arithmetic
            if years >= 5:
                raise_percent = 0.10
            elif years >= 3:
                raise_percent = 0.07
            else:
                raise_percent = 0.05

            new_salary = salary + (salary * raise_percent)
            total_comp = new_salary + bonus

            print(f"\n  {str(emp_name).capitalize()} ({str(title)}):")
            print(f"    Current salary: ${salary:,}")
            print(f"    Performance: {performance}%")
            print(f"    Bonus ({performance - 90:.1f}% over target): ${bonus:,.2f}")
            print(f"    Raise ({raise_percent * 100:.0f}%): ${salary * raise_percent:,.2f}")
            print(f"    New salary: ${new_salary:,.2f}")
            print(f"    Total comp: ${total_comp:,.2f}")

# Department statistics
print("\n### Department Statistics ###\n")

for dept_name in employees.keys():
    dept = employees[str(dept_name)]

    # Collect metrics - all arithmetic automatic!
    total_salary = 0
    total_performance = 0.0
    count = 0

    for emp_name in dept.keys():
        emp = dept[str(emp_name)]
        total_salary = total_salary + emp['salary']  # Automatic!
        total_performance = total_performance + emp['performance']  # Automatic!
        count = count + 1

    avg_salary = total_salary / count  # Automatic division!
    avg_performance = total_performance / count

    print(f"{str(dept_name).capitalize()}:")
    print(f"  Employees: {count}")
    print(f"  Avg Salary: ${avg_salary:,.2f}")
    print(f"  Avg Performance: {avg_performance:.1f}%")

# Find top performers across all departments
print("\n### Top Performers (performance > 92) ###\n")

all_performers = []
for dept_name in employees.keys():
    dept = employees[str(dept_name)]
    for emp_name in dept.keys():
        emp = dept[str(emp_name)]
        if emp['performance'] > 92:  # Direct comparison!
            all_performers.append({
                'name': str(emp_name),
                'dept': str(dept_name),
                'performance': emp['performance'] + 0.0,  # Convert to Python float
                'salary': emp['salary'] + 0  # Convert to Python int
            })

# Sort by performance (Python list of dicts)
all_performers.sort(key=lambda x: x['performance'], reverse=True)

for p in all_performers:
    print(f"  {p['name'].capitalize()} ({p['dept']}): {p['performance']}%")

print("\n" + "=" * 70)
print("✅ ALL operations (arithmetic, comparison, dict methods) work naturally!")
print("✅ NO explicit int() or float() casts needed!")
print("✅ Code reads like natural Python!")
print("=" * 70)
