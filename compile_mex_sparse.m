clear all;
tft_clear();
randn('seed',0);

%% initialize test model data
movie_index = Index(177);
user_index = Index(480);
topic_index = Index(5000);

X = Tensor( movie_index, user_index );
Z1 = Tensor( topic_index, movie_index);
Z2 = Tensor( topic_index, user_index );

Z1.data = sparse( topic_index.cardinality, movie_index.cardinality );
Z2.data = sparse( topic_index.cardinality, user_index.cardinality );

% initialize with random data, 20% sparsity
sparsity = 0.20
Z1.data = ( rand(topic_index.cardinality, movie_index.cardinality) > (1-sparsity) ) .* rand(topic_index.cardinality, movie_index.cardinality);
Z2.data = ( rand(topic_index.cardinality, user_index.cardinality) > (1-sparsity) ) .* rand(topic_index.cardinality, user_index.cardinality);

% prepare base case result
X_dot_product = Z2.data' * Z1.data;

pre_process();

% fpermissive is required to conform with gtp(X, Z1, Z2) syntax, otherwise syntax must be X=gtp(Z1,Z2)
%mex -largeArrayDims CXXFLAGS='-std=c++11 -fPIC -fpermissive'  gtp_mex.cpp % c++11 for print mutex lock
mex -largeArrayDims CXXFLAGS='-fPIC -fpermissive'  gtp_mex.cpp
gtp_full_time = tic;
gtp_mex(16, X, Z1, Z2);
display( [ 'gtp_mex time: ' num2str(toc(gtp_full_time)) ] );