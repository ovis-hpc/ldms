# Configuration file for the Sphinx documentation builder.

# -- Project information

project = 'LDMS'
copyright = '2025, Sandia National Laboratories and Open Grid Computing, Inc.'
author = 'SNL/OGC'

release = '0.1'
version = '0.1.0'

# Import necessary libraries
from docutils.parsers.rst import roles
from pathlib import Path
import os
import sys
import shutil

# Project root (parent directory of docs/)
project_root = Path(__file__).parent.parent

# Execute symlink creation immediately when conf.py loads
# This ensures files exist before Sphinx processes toctree directives
rst_man_dir = Path(__file__).parent / 'rst_man'

# Find all .rst files in the ldms/ directory
ldms_dir = project_root / 'ldms'
if ldms_dir.exists():
    for root, _, files in os.walk(str(ldms_dir)):
        for file in files:
            if file.endswith('.rst'):
                # Source file
                source = Path(root) / file

                # Create relative path structure in rst_man
                rel_path = Path(root).relative_to(ldms_dir)
                target_dir = rst_man_dir / rel_path
                os.makedirs(target_dir, exist_ok=True)

                # Create target path
                target = target_dir / file

                # Create symlink if it doesn't exist
                if not target.exists():
                    # Use relative path for symlink
                    rel_source = os.path.relpath(source, target.parent)
                    try:
                        os.symlink(rel_source, target)
                    except OSError:
                        # If symlink fails, copy the file
                        shutil.copy2(source, target)

# -- General configuration

extensions = [
    'sphinx.ext.duration',
    'sphinx.ext.doctest',
    'sphinx.ext.autodoc',
    'sphinx.ext.autosummary',
    'sphinx.ext.intersphinx',
]

def dummy_role(name, rawtext, text, lineno, inliner, options={}, content=[]):
    """A no-op role that prevents errors for unknown roles like :ref: in rst2man."""
    return [], []

# Register the dummy role for 'ref'
roles.register_local_role("ref", dummy_role)

# Keep the setup function to maintain compatibility, but now it does nothing related to symlinks
def setup(app):
    """This function is kept for compatibility but symlinks are now created earlier."""
    return app

intersphinx_mapping = {
    'python': ('https://docs.python.org/3/', None),
    'sphinx': ('https://www.sphinx-doc.org/en/master/', None),

    # Link to the "apis" of the "hpc-ovis" project and subprojects
    "ovis-hpc": ("https://ovis-hpc.readthedocs.io/en/latest/", None),
    "sos": ("https://ovis-hpc.readthedocs.io/projects/sos/en/latest/", None),
    "maestro": ("https://ovis-hpc.readthedocs.io/projects/maestro/en/latest/", None),
    "baler": ("https://ovis-hpc.readthedocs.io/projects/baler/en/latest/", None),
    "ldms": ("https://ovis-hpc.readthedocs.io/projects/ldms/en/latest/", None),
    "containers": ("https://ovis-hpc.readthedocs.io/projects/containers/en/latest/", None),
}

intersphinx_disabled_domains = ['std']
intersphinx_disabled_reftypes = ["*"]

templates_path = ['_templates']

# -- Options for HTML output

html_theme = 'sphinx_rtd_theme'
html_logo = 'https://ovis-hpc.readthedocs.io/en/latest/_images/ovis-logo.png'
html_theme_options = {
    'logo_only': True,
    'display_version': False,
    'navigation_depth': 6,
}

# -- Options for EPUB output
epub_show_urls = 'footnote'
