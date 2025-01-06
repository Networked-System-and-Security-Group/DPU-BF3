import os

def create_large_file(file_path, size_bytes):
    """Create a large file with the specified size in bytes."""
    # Option 1: Fill with random data
    # with open(file_path, 'wb') as f:
    #     f.write(os.urandom(size_bytes))

    # Option 2: Fill with repeating pattern (more compressible)
    block_size = 1024 * 1024  # 1MB blocks for efficiency
    pattern = b'abcd' * (block_size // 4)  # Adjust pattern to fit block size
    with open(file_path, 'wb') as f:
        for _ in range(size_bytes // block_size):
            f.write(pattern)
        f.write(pattern[:size_bytes % block_size])  # Write remaining bytes

if __name__ == "__main__":
    # Define the size of the file you want to generate
    file_size_gb = 1  # Change this value to 0.5 for 500MB or 1 for 1GB
    file_size_bytes = int(file_size_gb*  1024 * 1024)
    
    # Create the file
    output_file = 'large_file.bin'
    create_large_file(output_file, file_size_bytes)
    print(f"File '{output_file}' created with size {file_size_gb} GB.")