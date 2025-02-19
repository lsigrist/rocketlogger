"use strict";

export { AggregatingBuffer, AggregatingDataStore, AggregatingBinaryStore, MaxAggregatingDataStore, MinAggregatingDataStore };


class AggregatingBuffer {
    constructor(TypedArrayT, capacity, levels, aggregation_factor) {
        if (!TypedArrayT.hasOwnProperty('BYTES_PER_ELEMENT')) {
            throw TypeError('supporting TypedArray types only');
        }
        this._capacity = capacity;
        this._size = 0;
        this._levels = levels;
        this._aggregation_factor = aggregation_factor;
        this._data = new TypedArrayT(this._levels * this._capacity);
        this._dataLevel = Array.from({ length: this._levels },
            (_, i) => this._data.subarray(i * this._capacity, (i + 1) * this._capacity));
    }

    add(data) {
        const aggregate_level = Math.floor(this._size / this._capacity);
        // aggregate data about to be dequeued to next lower buffer
        for (let i = 1; i < this._levels; i++) {
            const aggregate_count = data.length / (this._aggregation_factor ** (this._levels - i));
            this.constructor._enqueue_aggregate(this._dataLevel[i], this._dataLevel[i - 1], aggregate_count, this._aggregation_factor);
        }

        // enqueue new data
        this.constructor._enqueue(data, this._dataLevel[this._levels - 1]);
        this._size += Math.floor(data.length / (this._aggregation_factor ** aggregate_level));
    }

    size() {
        return this._size;
    }

    getView() {
        if (this._size == 0) {
            return this._data.subarray(0, 0);
        }
        return this._data.subarray(-this._size);
    }

    // enqueue typed array data at end of typed array buffer
    static _enqueue(buffer_in, buffer_out) {
        buffer_out.set(buffer_out.subarray(buffer_in.length));
        buffer_out.set(buffer_in, buffer_out.length - buffer_in.length);
    }

    // enqueue aggregates of a typed array at end of typed array buffer
    static _enqueue_aggregate(buffer_in, buffer_out, count, aggregation_factor) {
        buffer_out.set(buffer_out.subarray(count));
        for (let i = buffer_out.length - count, offset_in = 0; i < buffer_out.length; i++, offset_in += aggregation_factor) {
            buffer_out[i] = buffer_in[offset_in];
        }
    }
}


class AggregatingDataStore extends AggregatingBuffer {
    constructor(TypedArrayT, size, levels, aggregation_factor) {
        if (!TypedArrayT.name.startsWith('Float')) {
            throw TypeError('supports only floating point types');
        }
        super(TypedArrayT, size, levels, aggregation_factor);
    }

    prepend(data) {
        let index_start = this._data.length - this._size;
        for (let i = 0; i < this._levels; i++) {
            // skip to next non-full buffer level
            if (index_start > this._capacity) {
                index_start -= this._capacity;
                continue;
            }

            // insert data limited to non-full buffer level
            const insert_size = Math.min(index_start, data.length);
            this._dataLevel[i].set(data.subarray(data.length - insert_size), index_start - insert_size);
            this._size += insert_size;
            break;
        }
    }
}


class AggregatingBinaryStore extends AggregatingBuffer {
    constructor(TypedArrayT, size, levels, aggregation_factor) {
        if (TypedArrayT.name !== 'Uint16Array') {
            throw TypeError('supports Uint16Array type only');
        }
        super(TypedArrayT, size, levels, aggregation_factor);
    }

    static _enqueue_aggregate(buffer_in, buffer_out, count, aggregation_factor) {
        buffer_out.set(buffer_out.subarray(count));
        for (let i = buffer_out.length - count, offset_in = 0; i < buffer_out.length; i++) {
            let min = 0x00ff;
            let max = 0x00ff;
            for (let j = 0; j < aggregation_factor; j++, offset_in++) {
                const value = buffer_in[offset_in];
                min &= value;
                max |= value;
            }
            buffer_out[i] = max & (0xff00 | min);
        }
    }
}


class MaxAggregatingDataStore extends AggregatingDataStore {
    static _enqueue_aggregate(buffer_in, buffer_out, count, aggregation_factor) {
        buffer_out.set(buffer_out.subarray(count));
        for (let i = buffer_out.length - count, offset_in = 0; i < buffer_out.length; i++) {
            let max = buffer_in[offset_in++];
            for (let j = 1; j < aggregation_factor; j++, offset_in++) {
                const value = buffer_in[offset_in];
                if (max < value) {
                    max = value;
                }
            }
            buffer_out[i] = max;
        }
    }
}

class MinAggregatingDataStore extends AggregatingDataStore {
    static _enqueue_aggregate(buffer_in, buffer_out, count, aggregation_factor) {
        buffer_out.set(buffer_out.subarray(count));
        for (let i = buffer_out.length - count, offset_in = 0; i < buffer_out.length; i++) {
            let min = buffer_in[offset_in++];
            for (let j = 1; j < aggregation_factor; j++, offset_in++) {
                const value = buffer_in[offset_in];
                if (min > value) {
                    min = value;
                }
            }
            buffer_out[i] = min;
        }
    }
}
