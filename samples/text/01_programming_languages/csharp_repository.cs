using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace WinPreview.Services
{
    public interface IDocumentRepository<T> where T : class
    {
        Task<T?> GetByIdAsync(Guid id);
        Task<IReadOnlyList<T>> GetAllActiveAsync();
        Task SaveAsync(T entity);
    }

    public class DocumentRecord
    {
        public Guid Id { get; init; } = Guid.NewGuid();
        public string Title { get; set; } = string.Empty;
        public long SizeBytes { get; set; }
        public DateTime LastModifiedUtc { get; set; } = DateTime.UtcNow;
        public bool IsArchived { get; set; }
    }

    public class DocumentRepository : IDocumentRepository<DocumentRecord>
    {
        private readonly List<DocumentRecord> _store = new();

        public Task<DocumentRecord?> GetByIdAsync(Guid id) =>
            Task.FromResult(_store.FirstOrDefault(d => d.Id == id));

        public Task<IReadOnlyList<DocumentRecord>> GetAllActiveAsync() =>
            Task.FromResult<IReadOnlyList<DocumentRecord>>(_store.Where(d => !d.IsArchived).ToList());

        public Task SaveAsync(DocumentRecord entity)
        {
            var idx = _store.FindIndex(d => d.Id == entity.Id);
            if (idx >= 0) _store[idx] = entity;
            else _store.Add(entity);
            return Task.CompletedTask;
        }
    }
}
