# Contributing to ST25R3916 ESPHome Component

Thank you for considering contributing to the ST25R3916 ESPHome component! This document provides guidelines and instructions for contributing.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [How Can I Contribute?](#how-can-i-contribute)
- [Development Setup](#development-setup)
- [Coding Standards](#coding-standards)
- [Testing](#testing)
- [Pull Request Process](#pull-request-process)
- [Issue Reporting](#issue-reporting)

## Code of Conduct

### Our Pledge

We are committed to providing a welcoming and inspiring community for all. Please be respectful and constructive in all interactions.

### Expected Behavior

- Be respectful and inclusive
- Accept constructive criticism gracefully
- Focus on what is best for the community
- Show empathy towards other community members

### Unacceptable Behavior

- Harassment or discriminatory language
- Trolling or insulting comments
- Public or private harassment
- Publishing others' private information without permission

## How Can I Contribute?

### Reporting Bugs

Before creating bug reports, please check existing issues to avoid duplicates.

**Good bug reports include:**

- Clear, descriptive title
- Exact steps to reproduce
- Expected vs actual behavior
- Your ESPHome version and hardware details
- Log output if applicable
- Screenshots if relevant

**Bug Report Template:**

```markdown
**Environment:**
- ESPHome version: 
- ESP board: 
- ST25R3916 version (A/B): 

**Description:**
[Clear description of the bug]

**Steps to Reproduce:**
1. 
2. 
3. 

**Expected Behavior:**
[What you expected to happen]

**Actual Behavior:**
[What actually happened]

**Logs:**
```
[Paste relevant logs]
```

**Additional Context:**
[Any other relevant information]
```

### Suggesting Features

Feature suggestions are welcome! Please:

1. Check if the feature already exists or is planned
2. Provide clear use cases
3. Describe the expected behavior
4. Consider implementation complexity

**Feature Request Template:**

```markdown
**Feature Description:**
[Clear description of the feature]

**Use Case:**
[Why is this feature needed?]

**Proposed Implementation:**
[If you have ideas on how to implement it]

**Alternatives Considered:**
[Other approaches you've thought about]
```

### Contributing Code

1. **Fork the repository**
2. **Create a feature branch** (`git checkout -b feature/amazing-feature`)
3. **Make your changes** following our coding standards
4. **Test thoroughly**
5. **Commit your changes** (`git commit -m 'Add amazing feature'`)
6. **Push to your fork** (`git push origin feature/amazing-feature`)
7. **Open a Pull Request**

## Development Setup

### Prerequisites

- Python 3.8 or higher
- Git
- ESPHome installed
- ESP32 development board
- ST25R3916 reader hardware

### Setup Instructions

```bash
# Clone your fork
git clone https://github.com/yourusername/esphome-st25r3916.git
cd esphome-st25r3916

# Install ESPHome
pip install esphome

# Install development dependencies
pip install black flake8 pylint

# Test compilation
esphome compile example-basic.yaml
```

### Development Workflow

1. **Make changes** in your feature branch
2. **Test locally** on actual hardware
3. **Run linters** before committing
4. **Commit with clear messages**
5. **Push and create PR**

## Coding Standards

### Python Code

We follow PEP 8 style guidelines:

```bash
# Format code with Black
black .

# Check with flake8
flake8 . --count --select=E9,F63,F7,F82 --show-source

# Lint with pylint
pylint *.py
```

**Python Guidelines:**
- Use 4 spaces for indentation
- Maximum line length: 100 characters
- Use descriptive variable names
- Add docstrings to functions
- Type hints where appropriate

**Example:**

```python
def read_register(self, reg: ST25R3916Register) -> uint8_t:
    """
    Read a single register from ST25R3916.
    
    Args:
        reg: Register address to read
        
    Returns:
        Register value
    """
    self.enable()
    value = self.read_byte(ST25R3916_READ_REG | (reg & 0x3F))
    self.disable()
    return value
```

### C++ Code

We follow ESPHome C++ conventions:

```bash
# Format with clang-format
clang-format -i st25r3916.cpp st25r3916.h
```

**C++ Guidelines:**
- Use 2 spaces for indentation
- Member variables end with underscore (`tag_present_`)
- Use `const` and `constexpr` where applicable
- Add comments for complex logic
- Follow ESPHome naming conventions

**Example:**

```cpp
bool ST25R3916::read_uid_(std::string &uid_string) {
  uint8_t uid[10];
  uint8_t uid_length = 0;
  
  // Wakeup field
  if (!this->iso14443a_wakeup_()) {
    return false;
  }
  
  // Read ATQA
  uint8_t atqa[2];
  if (!this->iso14443a_reqa_(atqa)) {
    ESP_LOGW(TAG, "Failed to read ATQA");
    return false;
  }
  
  // ... rest of implementation
}
```

### YAML Configuration

**Guidelines:**
- Use 2 spaces for indentation
- Keep consistent ordering
- Add comments for complex configurations
- Test all examples

## Testing

### Manual Testing

Before submitting a PR:

1. **Compile test:**
   ```bash
   esphome compile example-basic.yaml
   ```

2. **Hardware test:**
   - Flash to actual hardware
   - Test tag detection
   - Verify logs
   - Test all callbacks

3. **Integration test:**
   - Test with Home Assistant
   - Verify events are sent
   - Check sensor updates

### Automated Testing

Our CI pipeline runs:
- Compilation tests for all examples
- Code linting and formatting
- ESPHome version compatibility
- Documentation link checking

### Adding Tests

When adding new features:

1. Create example configuration showing usage
2. Document in README
3. Add to CI test matrix if needed
4. Test on actual hardware

## Pull Request Process

### Before Submitting

- [ ] Code compiles without errors
- [ ] Tested on actual hardware
- [ ] All linters pass
- [ ] Documentation updated
- [ ] Examples added/updated if needed
- [ ] CHANGELOG.md updated

### PR Description Template

```markdown
## Description
[Clear description of changes]

## Type of Change
- [ ] Bug fix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] Breaking change (fix or feature that would cause existing functionality to not work as expected)
- [ ] Documentation update

## Testing
- [ ] Compiled successfully
- [ ] Tested on ESP32
- [ ] Tested with ST25R3916
- [ ] All examples work

## Hardware Tested
- ESP board: 
- ST25R3916 version: 

## Checklist
- [ ] Code follows style guidelines
- [ ] Self-review completed
- [ ] Comments added to complex code
- [ ] Documentation updated
- [ ] No new warnings generated
- [ ] Examples updated/added

## Screenshots (if applicable)
[Add screenshots or logs]
```

### Review Process

1. Maintainers will review your PR
2. Address any feedback
3. Once approved, PR will be merged
4. Your contribution will be credited in CHANGELOG

### After Merge

- Your changes will be in the next release
- You'll be added to contributors list
- Consider helping with issues/discussions

## Issue Reporting

### Issue Labels

- `bug` - Something isn't working
- `enhancement` - New feature request
- `documentation` - Documentation improvements
- `good first issue` - Good for newcomers
- `help wanted` - Extra attention needed
- `question` - Further information requested

### Issue Lifecycle

1. **Open** - Issue reported
2. **Triage** - Reviewed by maintainers
3. **In Progress** - Someone is working on it
4. **Review** - PR submitted, under review
5. **Closed** - Fixed or resolved

## Style Guides

### Git Commit Messages

- Use present tense ("Add feature" not "Added feature")
- Use imperative mood ("Move cursor to..." not "Moves cursor to...")
- Limit first line to 72 characters
- Reference issues and PRs

**Examples:**
```
Add support for 10-byte UIDs

- Implement cascade level 3 support
- Add UID length validation
- Update documentation

Fixes #123
```

### Documentation

- Use clear, concise language
- Add code examples
- Include screenshots where helpful
- Keep README updated
- Document all public APIs

## Community

### Getting Help

- **GitHub Discussions** - For questions and general discussion
- **GitHub Issues** - For bugs and feature requests
- **ESPHome Discord** - For real-time chat

### Recognition

Contributors are recognized in:
- README.md contributors section
- CHANGELOG.md
- GitHub contributors page
- Release notes

## License

By contributing, you agree that your contributions will be licensed under the MIT License.

## Questions?

If you have questions about contributing, please:
1. Check existing documentation
2. Search closed issues
3. Ask in GitHub Discussions
4. Contact maintainers

Thank you for contributing! 🎉
