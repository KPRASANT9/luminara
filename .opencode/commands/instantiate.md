# /instantiate — Generate a New F-OS Domain

Create a new domain embedder from a physics description.
This command generates domain-specific files from the universal F-OS framework.

## Instructions

You are @architect. The user will provide:
- Domain name: $DOMAIN (e.g., "supply_chain", "retail_demand", "manufacturing")
- Physics description: governing equations, state variables, signals
- Sub-scores: what dimensions of F matter for this domain

Generate:
1. `src/embed/{domain}.py` — DomainEmbedder subclass with:
   - ROW_LABELS: physics state variables (with units)
   - COL_LABELS: timescale tiers relevant to this domain
   - compute_matrix(): runs governing equations → DomainMatrix
   - default_projection(): initial W_d from domain expertise
   - get_params() / set_params(): for Loop 2 access
   
2. `tests/physics/test_{domain}_regression.py` — physics regression tests

3. Updated `src/core/types.py` — add sub-scores if new ones needed

## Template (what the generated embed file must follow):

```python
from src.core.fos import DomainEmbedder, DomainMatrix, ProjectionMatrix

class {Domain}Embedder(DomainEmbedder):
    ROW_LABELS = [...]  # Physics state variables
    COL_LABELS = [...]  # Timescale tiers
    
    def compute_matrix(self, **inputs) -> DomainMatrix:
        # Governing equations here. STRUCTURE is fixed (prepare.py).
        # Parameters are tunable (train.py).
        ...
    
    def default_projection(self) -> ProjectionMatrix:
        # W_d: maps physics variables to sub-score components.
        # This is the initial guess. Embedding Ratchet will optimize.
        ...
```

## Rules
- Every row in the DomainMatrix must have physical UNITS
- Every governing equation must be cited (reference paper)
- The projection W_d must map physics → sub-score components
- I4 must be verifiable (gradient decomposes through the projection)
- Graceful degradation: missing data → neutral 0.5, never fabricate

RUN echo "Domain: $DOMAIN"
