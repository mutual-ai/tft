classdef Tensor < handle

    properties
        data = []; % raw data
        indices = {}; % cell of Index objects
        index_ids = []; % array of index ids used by this tensor
        reshaped = false; % flag indicating if raw data is reshaped by pre_process.m
        original_indices_permute_array = []; % permutation array, which can be used to convert raw data to its original shape
        name = ''; % name of the variable storing this Tensor object, set by pre_process.m
        is_fixed = 0;

        tft_indices = []; % local copy of the global variable, required for parfor executions
        id = -1;
    end

    methods

        function obj = Tensor(varargin)
            for i = 1:length(varargin)
                assert( isa(varargin{i}, 'Index'), 'Tensor:Tensor', 'Tensor constructor arguments must be of type Index' )
                obj.indices{end+1} = varargin{i};
            end

            global TFT_Tensor_index
            if length(TFT_Tensor_index) == 0
                TFT_Tensor_index = 1;
            else
                TFT_Tensor_index = TFT_Tensor_index + 1;
            end
            obj.id = TFT_Tensor_index;
        end

        function sref = subsref(obj,s)

            if length(s) == 3
                sref = builtin('subsref', obj, s);
            else

                switch s(1).type
                  case '.'
                    sref = builtin('subsref', obj, s);
                  case '()'
                    % s.subs must contain an index configuration from index set of the full tensor, v \in C_I(I)
                    % TODO: indices cardinality check
                    % TODO: index number check, number of indices must be same as the number of elements of tft_indices
                    % TODO: implement vector element field indexing to default
                    tmp = s.subs;
                    if strcmp( tmp(1), 'get' ) ~= 1
                        sref = builtin('subsref',obj,s);

                    else
                        % remove indices with zero cardinality
                        strides = [obj.tft_indices.cardinality] .* (size(obj.data)~=1);
                        strides = strides( strides ~= 0 );
                        % first dimension has stride 1
                        strides = [ 1 cumprod(strides(1:end-1)) ];

                        % remove indices with zero cardinality
                        indices = cell2mat( s.subs(2:end) ) .* (size(obj.data)~=1);
                        indices = indices( indices ~= 0 );
                        % indices are in matlab index, starting from 1
                        indices = indices - 1;

                        index = sum( strides .* indices  ) + 1;
                        sref = obj.data( index );
                    end

                  case '{}'
                    % s.subs must contain a single scalar, which corresponds to a particular index configuration from
                    % the index set of the full tensor, v \in C_I(I)
                    % TODO: index cardinality check
                    % TODO: index number check, there must be only 1 index
                    % TODO: implement cell list element field indexing to default
                    if strcmp( s.subs(1), 'get' ) ~= 1
                        % TODO: do not calculate full_strides for every index operation
                        full_strides = [obj.tft_indices.cardinality];
                        full_strides = [ 1 cumprod( full_strides(1:end-1) ) ];

                        full_index = cell2mat(s.subs(2:end));

                        % full tensor index
                        tensor_indices = zeros(1, length(obj.tft_indices));
                        tensor_indices = ceil(mod( full_index ./ full_strides, [obj.tft_indices.cardinality] ));
                        % replace zeros generated by modulus operator with max cardinality
                        tensor_indices( tensor_indices == 0 ) = [obj.tft_indices( tensor_indices == 0 ).cardinality];

                        sref = obj.subsref( struct( 'type', '()', 'subs', { arrayfun( @(x) {x}, tensor_indices ) } ) );
                    end
                end
            end
        end

    end

end