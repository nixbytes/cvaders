import os
import sys

# Make the project root importable so ``import src.game`` resolves in pytest
# without an install step.
sys.path.insert(0, os.path.dirname(__file__))
