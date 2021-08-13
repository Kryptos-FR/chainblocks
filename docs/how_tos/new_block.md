# How to program a new block type

## Basics
TODO

## Determine input and output
TODO

## Determine parameters
TODO

## Implement "info" functions
TODO

### `static CBTypesInfo inputTypes()`
TODO

### `static CBTypesInfo outputTypes()`
TODO

### `static CBParametersInfo parameters()`

## Implement parameters accessors
TODO

### `CBVar getParam(int index)`
TODO

### `void setParam(int index, const CBVar &value)`
TODO

## Implement block functions
TODO

### `CBVar activate(CBContext *context, const CBVar &input)`
TODO

## What to store as fields?
TODO: which data, which types?

## Debugging
TODO

### Debugging environment configuration
TODO

### Debugging tricks

GDB:
p value
p value.valueType


----

other topics:
- dealing with variables
- dealing with table API

